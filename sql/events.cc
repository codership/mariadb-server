/*
   Copyright (c) 2005, 2013, Oracle and/or its affiliates.
   Copyright (c) 2017, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#include "mariadb.h"
#include "sql_priv.h"
#include "unireg.h"
#include "sql_parse.h"                          // check_access
#include "sql_base.h"                           // close_mysql_tables
#include "sql_show.h"                           // append_definer
#include "events.h"
#include "sql_db.h"                          // check_db_dir_existence
#include "sql_table.h"                       // write_bin_log
#include "tztime.h"                             // struct Time_zone
#include "sql_acl.h"                            // EVENT_ACL
#include "records.h"          // init_read_record, end_read_record
#include "event_data_objects.h"
#include "event_db_repository.h"
#include "event_queue.h"
#include "event_scheduler.h"
#include "sp_head.h" // for Stored_program_creation_ctx
#include "set_var.h"
#include "lock.h"   // lock_object_name
#include "wsrep_mysqld.h"

/**
  @addtogroup Event_Scheduler
  @{
*/

/*
 TODO list :
 - CREATE EVENT should not go into binary log! Does it now? The SQL statements
   issued by the EVENT are replicated.
   I have an idea how to solve the problem at failover. So the status field
   will be ENUM('DISABLED', 'ENABLED', 'SLAVESIDE_DISABLED').
   In this case when CREATE EVENT is replicated it should go into the binary
   as SLAVESIDE_DISABLED if it is ENABLED, when it's created as DISABLEd it
   should be replicated as disabled. If an event is ALTERed as DISABLED the
   query should go untouched into the binary log, when ALTERed as enable then
   it should go as SLAVESIDE_DISABLED. This is regarding the SQL interface.
   TT routines however modify mysql.event internally and this does not go the
   log so in this case queries has to be injected into the log...somehow... or
   maybe a solution is RBR for this case, because the event may go only from
   ENABLED to DISABLED status change and this is safe for replicating. As well
   an event may be deleted which is also safe for RBR.

 - Add logging to file

*/


/*
  If the user (un)intentionally removes an event directly from mysql.event
  the following sequence has to be used to be able to remove the in-memory
  counterpart.
  1. CREATE EVENT the_name ON SCHEDULE EVERY 1 SECOND DISABLE DO SELECT 1;
  2. DROP EVENT the_name

  In other words, the first one will create a row in mysql.event . In the
  second step because there will be a line, disk based drop will pass and
  the scheduler will remove the memory counterpart. The reason is that
  in-memory queue does not check whether the event we try to drop from memory
  is disabled. Disabled events are not kept in-memory because they are not
  eligible for execution.
*/

Event_queue *Events::event_queue;
Event_scheduler *Events::scheduler;
Event_db_repository *Events::db_repository;
ulong Events::opt_event_scheduler= Events::EVENTS_OFF;
ulong Events::startup_state= Events::EVENTS_OFF;
ulong Events::inited;


/*
  Compares 2 LEX strings regarding case.

  SYNOPSIS
    sortcmp_lex_string()
      s   First LEX_STRING
      t   Second LEX_STRING
      cs  Charset

  RETURN VALUE
   -1   s < t
    0   s == t
    1   s > t
*/

int sortcmp_lex_string(const LEX_CSTRING *s, const LEX_CSTRING *t,
                       const CHARSET_INFO *cs)
{
 return cs->coll->strnncollsp(cs, (uchar *) s->str, s->length,
                                  (uchar *) t->str, t->length);
}


/**
  Push an error into the error stack if the system tables are
  not up to date.
*/

bool Events::check_if_system_tables_error()
{
  DBUG_ENTER("Events::check_if_system_tables_error");

  if (!inited)
  {
    my_error(ER_EVENTS_DB_ERROR, MYF(0));
    DBUG_RETURN(TRUE);
  }

  DBUG_RETURN(FALSE);
}


/**
  Reconstructs interval expression from interval type and expression
  value that is in form of a value of the smallest entity:
  For
    YEAR_MONTH - expression is in months
    DAY_MINUTE - expression is in minutes

  SYNOPSIS
    Events::reconstruct_interval_expression()
      buf         Preallocated String buffer to add the value to
      interval    The interval type (for instance YEAR_MONTH)
      expression  The value in the lowest entity

  RETURN VALUE
    0  OK
    1  Error
*/

int
Events::reconstruct_interval_expression(String *buf, interval_type interval,
                                        longlong expression)
{
  ulonglong expr= expression;
  char tmp_buff[128], *end;
  bool close_quote= TRUE;
  int multipl= 0;
  char separator=':';

  switch (interval) {
  case INTERVAL_YEAR_MONTH:
    multipl= 12;
    separator= '-';
    goto common_1_lev_code;
  case INTERVAL_DAY_HOUR:
    multipl= 24;
    separator= ' ';
    goto common_1_lev_code;
  case INTERVAL_HOUR_MINUTE:
  case INTERVAL_MINUTE_SECOND:
    multipl= 60;
common_1_lev_code:
    buf->append('\'');
    end= longlong10_to_str(expression/multipl, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));
    expr= expr - (expr/multipl)*multipl;
    break;
  case INTERVAL_DAY_MINUTE:
  {
    ulonglong tmp_expr= expr;

    tmp_expr/=(24*60);
    buf->append('\'');
    end= longlong10_to_str(tmp_expr, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// days
    buf->append(' ');

    tmp_expr= expr - tmp_expr*(24*60);//minutes left
    end= longlong10_to_str(tmp_expr/60, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// hours

    expr= tmp_expr - (tmp_expr/60)*60;
    /* the code after the switch will finish */
    break;
  }
  case INTERVAL_HOUR_SECOND:
  {
    ulonglong tmp_expr= expr;

    buf->append('\'');
    end= longlong10_to_str(tmp_expr/3600, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// hours
    buf->append(':');

    tmp_expr= tmp_expr - (tmp_expr/3600)*3600;
    end= longlong10_to_str(tmp_expr/60, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// minutes

    expr= tmp_expr - (tmp_expr/60)*60;
    /* the code after the switch will finish */
    break;
  }
  case INTERVAL_DAY_SECOND:
  {
    ulonglong tmp_expr= expr;

    tmp_expr/=(24*3600);
    buf->append('\'');
    end= longlong10_to_str(tmp_expr, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// days
    buf->append(' ');

    tmp_expr= expr - tmp_expr*(24*3600);//seconds left
    end= longlong10_to_str(tmp_expr/3600, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// hours
    buf->append(':');

    tmp_expr= tmp_expr - (tmp_expr/3600)*3600;
    end= longlong10_to_str(tmp_expr/60, tmp_buff, 10);
    buf->append(tmp_buff, (uint) (end- tmp_buff));// minutes

    expr= tmp_expr - (tmp_expr/60)*60;
    /* the code after the switch will finish */
    break;
  }
  case INTERVAL_DAY_MICROSECOND:
  case INTERVAL_HOUR_MICROSECOND:
  case INTERVAL_MINUTE_MICROSECOND:
  case INTERVAL_SECOND_MICROSECOND:
  case INTERVAL_MICROSECOND:
    my_error(ER_NOT_SUPPORTED_YET, MYF(0), "MICROSECOND");
    return 1;
  case INTERVAL_QUARTER:
    expr/= 3;
    close_quote= FALSE;
    break;
  case INTERVAL_WEEK:
    expr/= 7;
    close_quote= FALSE;
    break;
  default:
    close_quote= FALSE;
    break;
  }
  if (close_quote)
    buf->append(separator);
  end= longlong10_to_str(expr, tmp_buff, 10);
  buf->append(tmp_buff, (uint) (end- tmp_buff));
  if (close_quote)
    buf->append('\'');

  return 0;
}


/**
  Create a new query string for removing executable comments
  for avoiding leak and keeping consistency of the execution
  on master and slave.

  @param[in] thd                 Thread handler
  @param[in] buf                 Query string

  @return
             0           ok
             1           error
*/
static int
create_query_string(THD *thd, String *buf)
{
  buf->length(0);
  /* Append the "CREATE" part of the query */
  if (thd->lex->create_info.or_replace())
  {
    if (buf->append(STRING_WITH_LEN("CREATE OR REPLACE ")))
      return 1;
  }
  else if (buf->append(STRING_WITH_LEN("CREATE ")))
    return 1;
  /* Append definer */
  append_definer(thd, buf, &(thd->lex->definer->user), &(thd->lex->definer->host));
  /* Append the left part of thd->query after "DEFINER" part */
  if (buf->append(thd->lex->stmt_definition_begin,
                  thd->lex->stmt_definition_end -
                  thd->lex->stmt_definition_begin))
    return 1;

  return 0;
}


/**
  Create a new event.

  @param[in,out]  thd            THD
  @param[in]      parse_data     Event's data from parsing stage

  In case there is an event with the same name (db) and
  IF NOT EXISTS is specified, an warning is put into the stack.
  @sa Events::drop_event for the notes about locking, pre-locking
  and Events DDL.

  @retval  FALSE  OK
  @retval  TRUE   Error (reported)
*/

bool
Events::create_event(THD *thd, Event_parse_data *parse_data)
{
  bool ret;
  bool event_already_exists;
  enum_binlog_format save_binlog_format;
  DBUG_ENTER("Events::create_event");

  if (unlikely(check_if_system_tables_error()))
    DBUG_RETURN(TRUE);

  /*
    Perform semantic checks outside of Event_db_repository:
    once CREATE EVENT is supported in prepared statements, the
    checks will be moved to PREPARE phase.
  */
  if (parse_data->check_parse_data(thd))
    DBUG_RETURN(TRUE);

  /* At create, one of them must be set */
  DBUG_ASSERT(parse_data->expression || parse_data->execute_at);

  if (check_access(thd, EVENT_ACL, parse_data->dbname.str, NULL, NULL, 0, 0))
    DBUG_RETURN(TRUE);
  WSREP_TO_ISOLATION_BEGIN(WSREP_MYSQL_DB, NULL, NULL);

  if (lock_object_name(thd, MDL_key::EVENT,
                       parse_data->dbname.str, parse_data->name.str))
    DBUG_RETURN(TRUE);

  if (check_db_dir_existence(parse_data->dbname.str))
  {
    my_error(ER_BAD_DB_ERROR, MYF(0), parse_data->dbname.str);
    DBUG_RETURN(TRUE);
  }

  if (parse_data->do_not_create)
    DBUG_RETURN(FALSE);
  /*
    Turn off row binlogging of this statement and use statement-based
    so that all supporting tables are updated for CREATE EVENT command.
  */
  save_binlog_format= thd->set_current_stmt_binlog_format_stmt();

  if (thd->lex->create_info.or_replace() && event_queue)
    event_queue->drop_event(thd, &parse_data->dbname, &parse_data->name);

  /* On error conditions my_error() is called so no need to handle here */
  if (!(ret= db_repository->create_event(thd, parse_data,
                                         &event_already_exists)))
  {
    Event_queue_element *new_element;
    bool dropped= 0;

    if (!event_already_exists)
    {
      if (!(new_element= new Event_queue_element()))
        ret= TRUE;                                // OOM
      else if ((ret= db_repository->load_named_event(thd, &parse_data->dbname,
                                                     &parse_data->name,
                                                     new_element)))
      {
        if (!db_repository->drop_event(thd, &parse_data->dbname,
                                       &parse_data->name, TRUE))
          dropped= 1;
        delete new_element;
      }
      else
      {
        /* TODO: do not ignore the out parameter and a possible OOM error! */
        bool created;
        if (event_queue)
          event_queue->create_event(thd, new_element, &created);
      }
    }
    /*
      binlog the create event unless it's been successfully dropped
    */
    if (!dropped)
    {
      /* Binlog the create event. */
      DBUG_ASSERT(thd->query() && thd->query_length());
      char buffer[1024];
      String log_query(buffer, sizeof(buffer), &my_charset_bin);
      if (create_query_string(thd, &log_query))
      {
        my_message_sql(ER_STARTUP,
                       "Event Error: An error occurred while creating query "
                       "string, before writing it into binary log.",
                       MYF(ME_ERROR_LOG));
        ret= true;
      }
      else
      {
        /*
          If the definer is not set or set to CURRENT_USER, the value
          of CURRENT_USER will be written into the binary log as the
          definer for the SQL thread.
        */
        ret= write_bin_log(thd, TRUE, log_query.ptr(), log_query.length());
      }
    }
  }

  thd->restore_stmt_binlog_format(save_binlog_format);

  if (!ret && Events::opt_event_scheduler == Events::EVENTS_OFF)
  {
    push_warning(thd, Sql_condition::WARN_LEVEL_WARN, ER_UNKNOWN_ERROR, 
      "Event scheduler is switched off, use SET GLOBAL event_scheduler=ON to enable it.");
  }

  DBUG_RETURN(ret);
#ifdef WITH_WSREP
wsrep_error_label:
  DBUG_RETURN(true);
#endif
}


/**
  Alter an event.

  @param[in,out] thd         THD
  @param[in]     parse_data  Event's data from parsing stage
  @param[in]     new_dbname  A new schema name for the event. Set in the case of
                             ALTER EVENT RENAME, otherwise is NULL.
  @param[in]     new_name    A new name for the event. Set in the case of
                             ALTER EVENT RENAME

  Parameter 'et' contains data about dbname and event name.
  Parameter 'new_name' is the new name of the event, if not null
  this means that RENAME TO was specified in the query
  @sa Events::drop_event for the locking notes.

  @retval  FALSE  OK
  @retval  TRUE   error (reported)
*/

bool
Events::update_event(THD *thd, Event_parse_data *parse_data,
                     LEX_CSTRING *new_dbname, LEX_CSTRING *new_name)
{
  int ret;
  enum_binlog_format save_binlog_format;
  Event_queue_element *new_element;

  DBUG_ENTER("Events::update_event");

  if (unlikely(check_if_system_tables_error()))
    DBUG_RETURN(TRUE);

  if (parse_data->check_parse_data(thd) || parse_data->do_not_create)
    DBUG_RETURN(TRUE);

  if (check_access(thd, EVENT_ACL, parse_data->dbname.str, NULL, NULL, 0, 0))
    DBUG_RETURN(TRUE);

  WSREP_TO_ISOLATION_BEGIN(WSREP_MYSQL_DB, NULL, NULL);

  if (lock_object_name(thd, MDL_key::EVENT,
                       parse_data->dbname.str, parse_data->name.str))
    DBUG_RETURN(TRUE);

  if (check_db_dir_existence(parse_data->dbname.str))
  {
    my_error(ER_BAD_DB_ERROR, MYF(0), parse_data->dbname.str);
    DBUG_RETURN(TRUE);
  }


  if (new_dbname)                               /* It's a rename */
  {
    /* Check that the new and the old names differ. */
    if ( !sortcmp_lex_string(&parse_data->dbname, new_dbname,
                             system_charset_info) &&
         !sortcmp_lex_string(&parse_data->name, new_name,
                             system_charset_info))
    {
      my_error(ER_EVENT_SAME_NAME, MYF(0));
      DBUG_RETURN(TRUE);
    }

    /*
      And the user has sufficient privileges to use the target database.
      Do it before checking whether the database exists: we don't want
      to tell the user that a database doesn't exist if they can not
      access it.
    */
    if (check_access(thd, EVENT_ACL, new_dbname->str, NULL, NULL, 0, 0))
      DBUG_RETURN(TRUE);

    /*
     Acquire mdl exclusive lock on target database name.
    */
    if (lock_object_name(thd, MDL_key::EVENT,
                         new_dbname->str, new_name->str))
      DBUG_RETURN(TRUE);

    /* Check that the target database exists */
    if (check_db_dir_existence(new_dbname->str))
    {
      my_error(ER_BAD_DB_ERROR, MYF(0), new_dbname->str);
      DBUG_RETURN(TRUE);
    }
  }

  /*
    Turn off row binlogging of this statement and use statement-based
    so that all supporting tables are updated for UPDATE EVENT command.
  */
  save_binlog_format= thd->set_current_stmt_binlog_format_stmt();

  /* On error conditions my_error() is called so no need to handle here */
  if (!(ret= db_repository->update_event(thd, parse_data,
                                         new_dbname, new_name)))
  {
    LEX_CSTRING dbname= new_dbname ? *new_dbname : parse_data->dbname;
    LEX_CSTRING name= new_name ? *new_name : parse_data->name;

    if (!(new_element= new Event_queue_element()))
      ret= TRUE;                                // OOM
    else if ((ret= db_repository->load_named_event(thd, &dbname, &name,
                                                   new_element)))
      delete new_element;
    else
    {
      /*
        TODO: check if an update actually has inserted an entry
        into the queue.
        If not, and the element is ON COMPLETION NOT PRESERVE, delete
        it right away.
      */
      if (event_queue)
        event_queue->update_event(thd, &parse_data->dbname, &parse_data->name,
                                  new_element);
      /* Binlog the alter event. */
      DBUG_ASSERT(thd->query() && thd->query_length());
      ret= write_bin_log(thd, TRUE, thd->query(), thd->query_length());
    }
  }

  thd->restore_stmt_binlog_format(save_binlog_format);
  DBUG_RETURN(ret);
#ifdef WITH_WSREP
wsrep_error_label:
  DBUG_RETURN(true);
#endif
}


/**
  Drops an event

  @param[in,out]  thd        THD
  @param[in]      dbname     Event's schema
  @param[in]      name       Event's name
  @param[in]      if_exists  When this is set and the event does not exist
                             a warning is pushed into the warning stack.
                             Otherwise the operation produces an error.

  @note Similarly to DROP PROCEDURE, we do not allow DROP EVENT
  under LOCK TABLES mode, unless table mysql.event is locked.  To
  ensure that, we do not reset & backup the open tables state in
  this function - if in LOCK TABLES or pre-locking mode, this will
  lead to an error 'Table mysql.event is not locked with LOCK
  TABLES' unless it _is_ locked. In pre-locked mode there is
  another barrier - DROP EVENT commits the current transaction,
  and COMMIT/ROLLBACK is not allowed in stored functions and
  triggers.

  @retval  FALSE  OK
  @retval  TRUE   Error (reported)
*/

bool
Events::drop_event(THD *thd, const LEX_CSTRING *dbname,
                   const LEX_CSTRING *name, bool if_exists)
{
  int ret;
  enum_binlog_format save_binlog_format;
  DBUG_ENTER("Events::drop_event");

  if (unlikely(check_if_system_tables_error()))
    DBUG_RETURN(TRUE);

  if (check_access(thd, EVENT_ACL, dbname->str, NULL, NULL, 0, 0))
    DBUG_RETURN(TRUE);

  WSREP_TO_ISOLATION_BEGIN(WSREP_MYSQL_DB, NULL, NULL);

  /*
    Turn off row binlogging of this statement and use statement-based so
    that all supporting tables are updated for DROP EVENT command.
  */
  save_binlog_format= thd->set_current_stmt_binlog_format_stmt();

  if (lock_object_name(thd, MDL_key::EVENT,
                       dbname->str, name->str))
    DBUG_RETURN(TRUE);
  /* On error conditions my_error() is called so no need to handle here */
  if (!(ret= db_repository->drop_event(thd, dbname, name, if_exists)))
  {
    if (event_queue)
      event_queue->drop_event(thd, dbname, name);
    /* Binlog the drop event. */
    DBUG_ASSERT(thd->query() && thd->query_length());
    ret= write_bin_log(thd, TRUE, thd->query(), thd->query_length());
  }

  thd->restore_stmt_binlog_format(save_binlog_format);
  DBUG_RETURN(ret);
#ifdef WITH_WSREP
wsrep_error_label:
  DBUG_RETURN(true);
#endif
}


/**
  Drops all events from a schema

  @note We allow to drop all events in a schema even if the
  scheduler is disabled. This is to not produce any warnings
  in case of DROP DATABASE and a disabled scheduler.

  @param[in,out]  thd  Thread
  @param[in]      db   ASCIIZ schema name
*/

void
Events::drop_schema_events(THD *thd, const char *db)
{
  const LEX_CSTRING db_lex= { db, strlen(db) };

  DBUG_ENTER("Events::drop_schema_events");
  DBUG_PRINT("enter", ("dropping events from %s", db));

  DBUG_SLOW_ASSERT(ok_for_lower_case_names(db));

  /*
    Sic: no check if the scheduler is disabled or system tables
    are damaged, as intended.
  */
  if (event_queue)
    event_queue->drop_schema_events(thd, &db_lex);
  if (db_repository)
    db_repository->drop_schema_events(thd, &db_lex);
  else
  {
    if ((db_repository= new Event_db_repository))
    {
      db_repository->drop_schema_events(thd, &db_lex);
      delete db_repository;
      db_repository= 0;
    }
  }
  DBUG_VOID_RETURN;
}


/**
  A helper function to generate SHOW CREATE EVENT output from
  a named event
*/

static bool
send_show_create_event(THD *thd, Event_timed *et, Protocol *protocol)
{
  char show_str_buf[10 * STRING_BUFFER_USUAL_SIZE];
  String show_str(show_str_buf, sizeof(show_str_buf), system_charset_info);
  List<Item> field_list;
  LEX_CSTRING sql_mode;
  const String *tz_name;
  MEM_ROOT *mem_root= thd->mem_root;
  DBUG_ENTER("send_show_create_event");

  show_str.length(0);
  if (et->get_create_event(thd, &show_str))
    DBUG_RETURN(TRUE);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "Event", NAME_CHAR_LEN),
                       mem_root);

  if (sql_mode_string_representation(thd, et->sql_mode, &sql_mode))
    DBUG_RETURN(TRUE);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "sql_mode",
                                         (uint) sql_mode.length), mem_root);

  tz_name= et->time_zone->get_name();

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "time_zone", tz_name->length()),
                       mem_root);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "Create Event",
                                         show_str.length()), mem_root);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "character_set_client",
                                         MY_CS_NAME_SIZE), mem_root);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "collation_connection",
                                         MY_CS_NAME_SIZE), mem_root);

  field_list.push_back(new (mem_root)
                       Item_empty_string(thd, "Database Collation",
                                         MY_CS_NAME_SIZE), mem_root);

  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);

  protocol->prepare_for_resend();

  protocol->store(et->name.str, et->name.length, system_charset_info);
  protocol->store(sql_mode.str, sql_mode.length, system_charset_info);
  protocol->store(tz_name->ptr(), tz_name->length(), system_charset_info);
  protocol->store(show_str.ptr(), show_str.length(),
                  et->creation_ctx->get_client_cs());
  protocol->store(et->creation_ctx->get_client_cs()->csname,
                  strlen(et->creation_ctx->get_client_cs()->csname),
                  system_charset_info);
  protocol->store(et->creation_ctx->get_connection_cl()->name,
                  strlen(et->creation_ctx->get_connection_cl()->name),
                  system_charset_info);
  protocol->store(et->creation_ctx->get_db_cl()->name,
                  strlen(et->creation_ctx->get_db_cl()->name),
                  system_charset_info);

  if (protocol->write())
    DBUG_RETURN(TRUE);

  my_eof(thd);

  DBUG_RETURN(FALSE);
}


/**
  Implement SHOW CREATE EVENT statement

      thd   Thread context
      spn   The name of the event (db, name)

  @retval  FALSE  OK
  @retval  TRUE   error (reported)
*/

bool
Events::show_create_event(THD *thd, const LEX_CSTRING *dbname,
                          const LEX_CSTRING *name)
{
  Event_timed et;
  bool ret;

  DBUG_ENTER("Events::show_create_event");
  DBUG_PRINT("enter", ("name: %s@%s", dbname->str, name->str));

  if (unlikely(check_if_system_tables_error()))
    DBUG_RETURN(TRUE);

  if (check_access(thd, EVENT_ACL, dbname->str, NULL, NULL, 0, 0))
    DBUG_RETURN(TRUE);

  /*
    We would like to allow SHOW CREATE EVENT under LOCK TABLES and
    in pre-locked mode. mysql.event table is marked as a system table.
    This flag reduces the set of its participation scenarios in LOCK TABLES
    operation, and therefore an out-of-bound open of this table
    for reading like the one below (sic, only for reading) is
    more or less deadlock-free. For additional information about when a
    deadlock can occur please refer to the description of 'system table'
    flag.
  */
  ret= db_repository->load_named_event(thd, dbname, name, &et);

  if (!ret)
    ret= send_show_create_event(thd, &et, thd->protocol);

  DBUG_RETURN(ret);
}


/**
  Check access rights and fill INFORMATION_SCHEMA.events table.

  @param[in,out]  thd     Thread context
  @param[in]      tables  The temporary table to fill.

  In MySQL INFORMATION_SCHEMA tables are temporary tables that are
  created and filled on demand. In this function, we fill
  INFORMATION_SCHEMA.events. It is a callback for I_S module, invoked from
  sql_show.cc

  @return Has to be integer, as such is the requirement of the I_S API
  @retval  0  success
  @retval  1  an error, pushed into the error stack
*/

int
Events::fill_schema_events(THD *thd, TABLE_LIST *tables, COND * /* cond */)
{
  const char *db= NULL;
  int ret;
  char db_tmp[SAFE_NAME_LEN];
  DBUG_ENTER("Events::fill_schema_events");

  /*
    If we didn't start events because of --skip-grant-tables, return an
    empty set
  */
  if (opt_noacl)
    DBUG_RETURN(0);

  if (unlikely(check_if_system_tables_error()))
    DBUG_RETURN(1);

  /*
    If it's SHOW EVENTS then thd->lex->select_lex.db is guaranteed not to
    be NULL. Let's do an assert anyway.
  */
  if (thd->lex->sql_command == SQLCOM_SHOW_EVENTS)
  {
    DBUG_ASSERT(thd->lex->first_select_lex()->db.str);
    if (!is_infoschema_db(&thd->lex->first_select_lex()->db) && // There is no events in I_S
        check_access(thd, EVENT_ACL, thd->lex->first_select_lex()->db.str,
                     NULL, NULL, 0, 0))
      DBUG_RETURN(1);
    db= normalize_db_name(thd->lex->first_select_lex()->db.str,
                          db_tmp, sizeof(db_tmp));
  }
  ret= db_repository->fill_schema_events(thd, tables, db);

  DBUG_RETURN(ret);
}


/**
  Initializes the scheduler's structures.

  @param  THD or null (if called by init)
  @param  opt_noacl_or_bootstrap
                     TRUE if there is --skip-grant-tables or --bootstrap
                     option. In that case we disable the event scheduler.

  @note   This function is not synchronized.

  @retval  FALSE   Perhaps there was an error, and the event scheduler
                   is disabled. But the error is not fatal and the
                   server start up can continue.
  @retval  TRUE    Fatal error. Startup must terminate (call unireg_abort()).
*/

bool
Events::init(THD *thd, bool opt_noacl_or_bootstrap)
{
  int err_no;
  bool res= FALSE;
  bool had_thd= thd != 0;
  DBUG_ENTER("Events::init");

  DBUG_ASSERT(inited == 0);

  /*
    Was disabled explicitly from the command line
  */
  if (opt_event_scheduler == Events::EVENTS_DISABLED ||
      opt_noacl_or_bootstrap)
    DBUG_RETURN(FALSE);

  /* We need a temporary THD during boot */
  if (!thd)
  {

    if (!(thd= new THD(0)))
    {
      res= TRUE;
      goto end;
    }
    /*
      The thread stack does not start from this function but we cannot
      guess the real value. So better some value that doesn't assert than
      no value.
    */
    thd->thread_stack= (char*) &thd;
    thd->store_globals();
    /*
      Set current time for the thread that handles events.
      Current time is stored in data member start_time of THD class.
      Subsequently, this value is used to check whether event was expired
      when make loading events from storage. Check for event expiration time
      is done at Event_queue_element::compute_next_execution_time() where
      event's status set to Event_parse_data::DISABLED and dropped flag set
      to true if event was expired.
    */
    thd->set_time();
  }

  /*
    We will need Event_db_repository anyway, even if the scheduler is
    disabled - to perform events DDL.
  */
  if (!(db_repository= new Event_db_repository))
  {
    res= TRUE; /* fatal error: request unireg_abort */
    goto end;
  }

  /*
    Since we allow event DDL even if the scheduler is disabled,
    check the system tables, as we might need them.

    If run with --skip-grant-tables or --bootstrap, don't try to do the
    check of system tables and don't complain: in these modes the tables
    are most likely not there and we're going to disable the event
    scheduler anyway.
  */
  if (Event_db_repository::check_system_tables(thd))
  {
    delete db_repository;
    db_repository= 0;
    my_message(ER_STARTUP,
               "Event Scheduler: An error occurred when initializing "
               "system tables. Disabling the Event Scheduler.",
               MYF(ME_ERROR_LOG));
    /* Disable the scheduler since the system tables are not up to date */
    opt_event_scheduler= EVENTS_OFF;
    goto end;
  }


  DBUG_ASSERT(opt_event_scheduler == Events::EVENTS_ON ||
              opt_event_scheduler == Events::EVENTS_OFF);

  if (!(event_queue= new Event_queue) ||
      !(scheduler= new Event_scheduler(event_queue)))
  {
    res= TRUE; /* fatal error: request unireg_abort */
    goto end;
  }

  if (event_queue->init_queue(thd) || load_events_from_db(thd) ||
      (opt_event_scheduler == EVENTS_ON && scheduler->start(&err_no)))
  {
    my_message_sql(ER_STARTUP,
                   "Event Scheduler: Error while loading from mysql.event table.",
                   MYF(ME_ERROR_LOG));
    res= TRUE; /* fatal error: request unireg_abort */
    goto end;
  }
  Event_worker_thread::init(db_repository);
  inited= 1;

end:
  if (res)
    deinit();
  if (!had_thd)
    delete thd;

  DBUG_RETURN(res);
}

/*
  Cleans up scheduler's resources. Called at server shutdown.

  SYNOPSIS
    Events::deinit()

  NOTES
    This function is not synchronized.
*/

void
Events::deinit()
{
  DBUG_ENTER("Events::deinit");

  delete scheduler;
  scheduler= NULL;                            /* For restart */
  delete event_queue;
  event_queue= NULL;                          /* For restart */
  delete db_repository;
  db_repository= NULL;                        /* For restart */

  inited= 0;
  DBUG_VOID_RETURN;
}

#ifdef HAVE_PSI_INTERFACE
PSI_mutex_key key_LOCK_event_queue,
              key_event_scheduler_LOCK_scheduler_state;

static PSI_mutex_info all_events_mutexes[]=
{
  { &key_LOCK_event_queue, "LOCK_event_queue", PSI_FLAG_GLOBAL},
  { &key_event_scheduler_LOCK_scheduler_state, "Event_scheduler::LOCK_scheduler_state", PSI_FLAG_GLOBAL}
};

PSI_cond_key key_event_scheduler_COND_state, key_COND_queue_state;

static PSI_cond_info all_events_conds[]=
{
  { &key_event_scheduler_COND_state, "Event_scheduler::COND_state", PSI_FLAG_GLOBAL},
  { &key_COND_queue_state, "COND_queue_state", PSI_FLAG_GLOBAL},
};

PSI_thread_key key_thread_event_scheduler, key_thread_event_worker;

static PSI_thread_info all_events_threads[]=
{
  { &key_thread_event_scheduler, "event_scheduler", PSI_FLAG_GLOBAL},
  { &key_thread_event_worker, "event_worker", 0}
};
#endif /* HAVE_PSI_INTERFACE */

PSI_stage_info stage_waiting_on_empty_queue= { 0, "Waiting on empty queue", 0};
PSI_stage_info stage_waiting_for_next_activation= { 0, "Waiting for next activation", 0};
PSI_stage_info stage_waiting_for_scheduler_to_stop= { 0, "Waiting for the scheduler to stop", 0};

#ifdef HAVE_PSI_INTERFACE
PSI_stage_info *all_events_stages[]=
{
  & stage_waiting_on_empty_queue,
  & stage_waiting_for_next_activation,
  & stage_waiting_for_scheduler_to_stop
};

static void init_events_psi_keys(void)
{
  const char* category= "sql";
  int count;

  count= array_elements(all_events_mutexes);
  mysql_mutex_register(category, all_events_mutexes, count);

  count= array_elements(all_events_conds);
  mysql_cond_register(category, all_events_conds, count);

  count= array_elements(all_events_threads);
  mysql_thread_register(category, all_events_threads, count);

  count= array_elements(all_events_stages);
  mysql_stage_register(category, all_events_stages, count);

}
#endif /* HAVE_PSI_INTERFACE */

/**
  Inits Events mutexes

  SYNOPSIS
    Events::init_mutexes()
      thd  Thread
*/

void
Events::init_mutexes()
{
#ifdef HAVE_PSI_INTERFACE
  init_events_psi_keys();
#endif
}


/*
  Dumps the internal status of the scheduler and the memory cache
  into a table with two columns - Name & Value. Different properties
  which could be useful for debugging for instance deadlocks are
  returned.

  SYNOPSIS
    Events::dump_internal_status()
*/

void
Events::dump_internal_status()
{
  DBUG_ENTER("Events::dump_internal_status");
  puts("\n\n\nEvents status:");
  puts("LLA = Last Locked At  LUA = Last Unlocked At");
  puts("WOC = Waiting On Condition  DL = Data Locked");

  /*
    opt_event_scheduler should only be accessed while
    holding LOCK_global_system_variables.
  */
  mysql_mutex_lock(&LOCK_global_system_variables);
  if (!inited)
    puts("The Event Scheduler is disabled");
  else
  {
    scheduler->dump_internal_status();
    event_queue->dump_internal_status();
  }

  mysql_mutex_unlock(&LOCK_global_system_variables);
  DBUG_VOID_RETURN;
}

bool Events::start(int *err_no)
{
  DBUG_ASSERT(inited);
  return scheduler->start(err_no);
}

bool Events::stop()
{
  DBUG_ASSERT(inited);
  return scheduler->stop();
}

/**
  Loads all ENABLED events from mysql.event into a prioritized
  queue.

  This function is called during the server start up. It reads
  every event, computes the next execution time, and if the event
  needs execution, adds it to a prioritized queue. Otherwise, if
  ON COMPLETION DROP is specified, the event is automatically
  removed from the table.

  @param[in,out] thd Thread context. Used for memory allocation in some cases.

  @retval  FALSE  success
  @retval  TRUE   error, the load is aborted

  @note Reports the error to the console
*/

bool
Events::load_events_from_db(THD *thd)
{
  TABLE *table;
  READ_RECORD read_record_info;
  bool ret= TRUE;
  uint count= 0;
  ulong saved_master_access;
  DBUG_ENTER("Events::load_events_from_db");
  DBUG_PRINT("enter", ("thd: %p", thd));

  /*
    NOTE: even if we run in read-only mode, we should be able to lock the
    mysql.event table for writing. In order to achieve this, we should call
    mysql_lock_tables() under the super user.

    Same goes for transaction access mode.
    Temporarily reset it to read-write.
  */

  saved_master_access= thd->security_ctx->master_access;
  thd->security_ctx->master_access |= SUPER_ACL;
  bool save_tx_read_only= thd->tx_read_only;
  thd->tx_read_only= false;

  ret= db_repository->open_event_table(thd, TL_WRITE, &table);

  thd->tx_read_only= save_tx_read_only;
  thd->security_ctx->master_access= saved_master_access;

  if (ret)
  {
    my_message_sql(ER_STARTUP,
                   "Event Scheduler: Failed to open table mysql.event",
                   MYF(ME_ERROR_LOG));
    DBUG_RETURN(TRUE);
  }

  if (init_read_record(&read_record_info, thd, table, NULL, NULL, 0, 1, FALSE))
  {
    close_thread_tables(thd);
    DBUG_RETURN(TRUE);
  }

  while (!(read_record_info.read_record()))
  {
    Event_queue_element *et;
    bool created, dropped;

    if (!(et= new Event_queue_element))
      goto end;

    DBUG_PRINT("info", ("Loading event from row."));

    if (et->load_from_row(thd, table))
    {
      my_message(ER_STARTUP,
                 "Event Scheduler: "
                 "Error while loading events from mysql.event. "
                 "The table probably contains bad data or is corrupted",
                 MYF(ME_ERROR_LOG));
      delete et;
      goto end;
    }

#ifdef WITH_WSREP
    /**
      If SST is done from a galera node that is also acting as MASTER
      newly synced node in galera eco-system will also copy-over the
      event state enabling duplicate event in galera eco-system.
      DISABLE such events if the current node is not event orginator.
      (Also, make sure you skip disabling it if is already disabled to avoid
       creation of redundant action)
      NOTE:
      This complete system relies on server-id. Ideally server-id should be
      same for all nodes of galera eco-system but they aren't same.
      Infact, based on galera use-case it seems like it recommends to have each
      node with different server-id.
    */
    if (WSREP(thd) && et->originator != thd->variables.server_id)
    {
        if (et->status == Event_parse_data::SLAVESIDE_DISABLED)
          continue;

        store_record(table, record[1]);
        table->field[ET_FIELD_STATUS]->
                store((longlong) Event_parse_data::SLAVESIDE_DISABLED,
                      TRUE);

	/* All the dmls to mysql.events tables are stmt bin-logged. */
        bool save_binlog_row_based;
        if ((save_binlog_row_based= thd->is_current_stmt_binlog_format_row()))
	  thd->set_current_stmt_binlog_format_stmt();

        (void) table->file->ha_update_row(table->record[1], table->record[0]);

        if (save_binlog_row_based)
          thd->set_current_stmt_binlog_format_row();

        delete et;
        continue;
    }
#endif /* WITH_WSREP */

    /**
      Since the Event_queue_element object could be deleted inside
      Event_queue::create_event we should save the value of dropped flag
      into the temporary variable.
    */
    dropped= et->dropped;
    if (event_queue->create_event(thd, et, &created))
    {
      /* Out of memory */
      delete et;
      goto end;
    }
    if (created)
      count++;
    else if (dropped)
    {
      /*
        If not created, a stale event - drop if immediately if
        ON COMPLETION NOT PRESERVE.
        XXX: This won't be replicated, thus the drop won't appear in
             in the slave. When the slave is restarted it will drop events.
             However, as the slave will be "out of sync", it might happen that
             an event created on the master, after master restart, won't be
             replicated to the slave correctly, as the create will fail there.
      */
      int rc= table->file->ha_delete_row(table->record[0]);
      if (rc)
      {
        table->file->print_error(rc, MYF(0));
        goto end;
      }
    }
  }
  my_printf_error(ER_STARTUP,
                  "Event Scheduler: Loaded %d event%s",
                  MYF(ME_ERROR_LOG |
                      (global_system_variables.log_warnings) ?
                      ME_NOTE: 0),
                  count, (count == 1) ? "" : "s");
  ret= FALSE;

end:
  end_read_record(&read_record_info);

  close_mysql_tables(thd);
  DBUG_RETURN(ret);
}

#ifdef WITH_WSREP
int wsrep_create_event_query(THD *thd, uchar** buf, size_t* buf_len)
{
  char buffer[1024];
  String log_query(buffer, sizeof(buffer), &my_charset_bin);

  if (create_query_string(thd, &log_query))
  {
    WSREP_WARN("events create string failed: schema: %s, query: %s",
               thd->get_db(), thd->query());
    return 1;
  }
  return wsrep_to_buf_helper(thd, log_query.ptr(), log_query.length(), buf, buf_len);
}
#endif /* WITH_WSREP */
/**
  @} (End of group Event_Scheduler)
*/

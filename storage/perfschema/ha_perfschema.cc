/* Copyright (c) 2008, 2014, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA */

/**
  @file storage/perfschema/ha_perfschema.cc
  Performance schema storage engine (implementation).
*/

#include "sql_plugin.h"
#include "my_pthread.h"
#include "ha_perfschema.h"
#include "pfs_engine_table.h"
#include "pfs_column_values.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "pfs_account.h"
#include "pfs_host.h"
#include "pfs_user.h"
#include "pfs_account.h"

handlerton *pfs_hton= NULL;

static handler* pfs_create_handler(handlerton *hton,
                                   TABLE_SHARE *table,
                                   MEM_ROOT *mem_root)
{
  return new (mem_root) ha_perfschema(hton, table);
}

static int compare_database_names(const char *name1, const char *name2)
{
  if (lower_case_table_names)
    return strcasecmp(name1, name2);
  return strcmp(name1, name2);
}

static const PFS_engine_table_share*
find_table_share(const char *db, const char *name)
{
  DBUG_ENTER("find_table_share");

  if (compare_database_names(db, PERFORMANCE_SCHEMA_str.str) != 0)
    DBUG_RETURN(NULL);

  const PFS_engine_table_share* result;
  result= PFS_engine_table::find_engine_table_share(name);
  DBUG_RETURN(result);
}

static int pfs_discover_table(handlerton *hton, THD *thd, TABLE_SHARE *share)
{
  const PFS_engine_table_share *pfs_share;

  if ((pfs_share= find_table_share(share->db.str, share->table_name.str)))
    return share->init_from_sql_statement_string(thd, false,
                                                 pfs_share->sql.str,
                                                 pfs_share->sql.length);
  return HA_ERR_NO_SUCH_TABLE;
}

static int pfs_discover_table_existence(handlerton *hton, const char *db,
                                        const char *table_name)
{
  return MY_TEST(find_table_share(db, table_name));
}

static int pfs_init_func(void *p)
{
  DBUG_ENTER("pfs_init_func");

  pfs_hton= reinterpret_cast<handlerton *> (p);

  pfs_hton->state= SHOW_OPTION_YES;
  pfs_hton->create= pfs_create_handler;
  pfs_hton->show_status= pfs_show_status;
  pfs_hton->flags= HTON_ALTER_NOT_SUPPORTED |
    HTON_TEMPORARY_NOT_SUPPORTED |
    HTON_NO_PARTITION |
    HTON_NO_BINLOG_ROW_OPT;

  /*
    As long as the server implementation keeps using legacy_db_type,
    as for example in mysql_truncate(),
    we can not rely on the fact that different mysqld process will assign
    consistently the same legacy_db_type for a given storage engine name.
    In particular, using different --loose-skip-xxx options between
    ./mysqld --bootstrap
    ./mysqld
    creates bogus .frm forms when bootstrapping the performance schema,
    if we rely on ha_initialize_handlerton to assign a really dynamic value.
    To fix this, a dedicated DB_TYPE is officially assigned to
    the performance schema. See Bug#43039.
  */
  pfs_hton->db_type= DB_TYPE_PERFORMANCE_SCHEMA;
  pfs_hton->discover_table= pfs_discover_table;
  pfs_hton->discover_table_existence= pfs_discover_table_existence;
  pfs_hton->discover_table_names= pfs_discover_table_names;

  PFS_engine_table_share::init_all_locks();

  DBUG_RETURN(0);
}

static int pfs_done_func(void *p)
{
  DBUG_ENTER("pfs_done_func");

  pfs_hton= NULL;

  PFS_engine_table_share::delete_all_locks();

  DBUG_RETURN(0);
}

static struct st_mysql_show_var pfs_status_vars[]=
{
  {"Performance_schema_mutex_classes_lost",
    (char*) &mutex_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_rwlock_classes_lost",
    (char*) &rwlock_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_cond_classes_lost",
    (char*) &cond_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_thread_classes_lost",
    (char*) &thread_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_file_classes_lost",
    (char*) &file_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_socket_classes_lost",
    (char*) &socket_class_lost, SHOW_LONG_NOFLUSH},
  {"Performance_schema_mutex_instances_lost",
    (char*) &mutex_lost, SHOW_LONG},
  {"Performance_schema_rwlock_instances_lost",
    (char*) &rwlock_lost, SHOW_LONG},
  {"Performance_schema_cond_instances_lost",
    (char*) &cond_lost, SHOW_LONG},
  {"Performance_schema_thread_instances_lost",
    (char*) &thread_lost, SHOW_LONG},
  {"Performance_schema_file_instances_lost",
    (char*) &file_lost, SHOW_LONG},
  {"Performance_schema_file_handles_lost",
    (char*) &file_handle_lost, SHOW_LONG},
  {"Performance_schema_socket_instances_lost",
    (char*) &socket_lost, SHOW_LONG},
  {"Performance_schema_locker_lost",
    (char*) &locker_lost, SHOW_LONG},
  /* table shares, can be flushed */
  {"Performance_schema_table_instances_lost",
    (char*) &table_share_lost, SHOW_LONG},
  /* table handles, can be flushed */
  {"Performance_schema_table_handles_lost",
    (char*) &table_lost, SHOW_LONG},
  {"Performance_schema_hosts_lost",
    (char*) &host_lost, SHOW_LONG},
  {"Performance_schema_users_lost",
    (char*) &user_lost, SHOW_LONG},
  {"Performance_schema_accounts_lost",
    (char*) &account_lost, SHOW_LONG},
  {"Performance_schema_stage_classes_lost",
    (char*) &stage_class_lost, SHOW_LONG},
  {"Performance_schema_statement_classes_lost",
    (char*) &statement_class_lost, SHOW_LONG},
  {"Performance_schema_digest_lost",
    (char*) &digest_lost, SHOW_LONG},
  {"Performance_schema_session_connect_attrs_lost",
    (char*) &session_connect_attrs_lost, SHOW_LONG},
  {NullS, NullS, SHOW_LONG}
};

struct st_mysql_storage_engine pfs_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

const char* pfs_engine_name= "PERFORMANCE_SCHEMA";

mysql_declare_plugin(perfschema)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &pfs_storage_engine,
  pfs_engine_name,
  "Marc Alff, Oracle", /* Formerly Sun Microsystems, formerly MySQL */
  "Performance Schema",
  PLUGIN_LICENSE_GPL,
  pfs_init_func,                                /* Plugin Init */
  pfs_done_func,                                /* Plugin Deinit */
  0x0001 /* 0.1 */,
  pfs_status_vars,                              /* status variables */
  NULL,                                         /* system variables */
  NULL,                                         /* config options */
  0,                                            /* flags */
}
mysql_declare_plugin_end;

maria_declare_plugin(perfschema)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &pfs_storage_engine,
  pfs_engine_name,
  "Marc Alff, Oracle",
  "Performance Schema",
  PLUGIN_LICENSE_GPL,
  pfs_init_func,
  pfs_done_func,
  0x0001,
  pfs_status_vars,
  NULL,
  "5.6.40",
  MariaDB_PLUGIN_MATURITY_STABLE
}
maria_declare_plugin_end;

ha_perfschema::ha_perfschema(handlerton *hton, TABLE_SHARE *share)
  : handler(hton, share), m_table_share(NULL), m_table(NULL)
{}

ha_perfschema::~ha_perfschema() = default;

int ha_perfschema::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_perfschema::open");

  m_table_share= find_table_share(table_share->db.str,
                                  table_share->table_name.str);
  if (! m_table_share)
    DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);

  thr_lock_data_init(m_table_share->m_thr_lock_ptr, &m_thr_lock, NULL);
  ref_length= m_table_share->m_ref_length;

  DBUG_RETURN(0);
}

int ha_perfschema::close(void)
{
  DBUG_ENTER("ha_perfschema::close");
  m_table_share= NULL;
  delete m_table;
  m_table= NULL;

  DBUG_RETURN(0);
}

int ha_perfschema::write_row(const uchar *buf)
{
  int result;

  DBUG_ENTER("ha_perfschema::write_row");
  if (!pfs_initialized)
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  DBUG_ASSERT(m_table_share);
  result= m_table_share->write_row(table, buf, table->field);
  DBUG_RETURN(result);
}

void ha_perfschema::use_hidden_primary_key(void)
{
  /*
    This is also called in case of row based replication,
    see TABLE::mark_columns_needed_for_update().
    Add all columns to the read set, but do not touch the write set,
    as some columns in the SETUP_ tables are not writable.
  */
  table->column_bitmaps_set_no_signal(&table->s->all_set, table->write_set);
}

int ha_perfschema::update_row(const uchar *old_data, const uchar *new_data)
{
  DBUG_ENTER("ha_perfschema::update_row");
  if (!pfs_initialized)
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  if (is_executed_by_slave())
    DBUG_RETURN(0);

  DBUG_ASSERT(m_table);
  int result= m_table->update_row(table, old_data, new_data, table->field);
  DBUG_RETURN(result);
}

int ha_perfschema::delete_row(const uchar *buf)
{
  DBUG_ENTER("ha_perfschema::delete_row");
  if (!pfs_initialized)
    DBUG_RETURN(HA_ERR_WRONG_COMMAND);

  DBUG_ASSERT(m_table);
  int result= m_table->delete_row(table, buf, table->field);
  DBUG_RETURN(result);
}

int ha_perfschema::rnd_init(bool scan)
{
  int result;
  DBUG_ENTER("ha_perfschema::rnd_init");

  DBUG_ASSERT(m_table_share);
  DBUG_ASSERT(m_table_share->m_open_table != NULL);

  stats.records= 0;
  if (m_table == NULL)
    m_table= m_table_share->m_open_table();
  else
    m_table->reset_position();

  if (m_table != NULL)
    m_table->rnd_init(scan);

  result= m_table ? 0 : HA_ERR_OUT_OF_MEM;
  DBUG_RETURN(result);
}

int ha_perfschema::rnd_end(void)
{
  DBUG_ENTER("ha_perfschema::rnd_end");
  DBUG_ASSERT(m_table);
  delete m_table;
  m_table= NULL;
  DBUG_RETURN(0);
}

int ha_perfschema::rnd_next(uchar *buf)
{
  DBUG_ENTER("ha_perfschema::rnd_next");
  if (!pfs_initialized)
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  DBUG_ASSERT(m_table);

  int result= m_table->rnd_next();
  if (result == 0)
  {
    result= m_table->read_row(table, buf, table->field);
    if (result == 0)
      stats.records++;
  }
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

void ha_perfschema::position(const uchar *record)
{
  DBUG_ENTER("ha_perfschema::position");

  DBUG_ASSERT(m_table);
  m_table->get_position(ref);
  DBUG_VOID_RETURN;
}

int ha_perfschema::rnd_pos(uchar *buf, uchar *pos)
{
  DBUG_ENTER("ha_perfschema::rnd_pos");
  if (!pfs_initialized)
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  DBUG_ASSERT(m_table);
  int result= m_table->rnd_pos(pos);
  if (result == 0)
    result= m_table->read_row(table, buf, table->field);
  table->status= (result ? STATUS_NOT_FOUND : 0);
  DBUG_RETURN(result);
}

int ha_perfschema::info(uint flag)
{
  DBUG_ENTER("ha_perfschema::info");
  DBUG_ASSERT(m_table_share);
  if (flag & HA_STATUS_VARIABLE)
    stats.records= m_table_share->get_row_count();
  if (flag & HA_STATUS_CONST)
    ref_length= m_table_share->m_ref_length;
  DBUG_RETURN(0);
}

int ha_perfschema::delete_all_rows(void)
{
  int result;

  DBUG_ENTER("ha_perfschema::delete_all_rows");
  if (!pfs_initialized)
    DBUG_RETURN(0);

  if (is_executed_by_slave())
    DBUG_RETURN(0);

  DBUG_ASSERT(m_table_share);
  if (m_table_share->m_delete_all_rows)
    result= m_table_share->m_delete_all_rows();
  else
  {
    result= HA_ERR_WRONG_COMMAND;
  }
  DBUG_RETURN(result);
}

int ha_perfschema::truncate()
{
  return delete_all_rows();
}

THR_LOCK_DATA **ha_perfschema::store_lock(THD *thd,
                                       THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && m_thr_lock.type == TL_UNLOCK)
    m_thr_lock.type= lock_type;
  *to++= &m_thr_lock;
  m_thr_lock.m_psi= m_psi;
  return to;
}

int ha_perfschema::delete_table(const char *name)
{
  DBUG_ENTER("ha_perfschema::delete_table");
  DBUG_RETURN(0);
}

int ha_perfschema::rename_table(const char * from, const char * to)
{
  DBUG_ENTER("ha_perfschema::rename_table ");
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

int ha_perfschema::create(const char *name, TABLE *table_arg,
                          HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_perfschema::create");
  /*
    This is not a general purpose engine.
    Failure to CREATE TABLE is the expected result.
  */
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

void ha_perfschema::print_error(int error, myf errflag)
{
  switch (error)
  {
    case HA_ERR_TABLE_NEEDS_UPGRADE:
      /*
        The error message for ER_TABLE_NEEDS_UPGRADE refers to REPAIR table,
        which does not apply to performance schema tables.
      */
      my_error(ER_WRONG_NATIVE_TABLE_STRUCTURE, MYF(0),
               table_share->db.str, table_share->table_name.str);
      break;
    case HA_ERR_WRONG_COMMAND:
      /*
        The performance schema is not a general purpose storage engine,
        some operations are not supported, by design.
        We do not want to print "Command not supported",
        which gives the impression that a command implementation is missing,
        and that the failure should be considered a bug.
        We print "Invalid performance_schema usage." instead,
        to emphasise that the operation attempted is not meant to be legal,
        and that the failure returned is indeed the expected result.
      */
      my_error(ER_WRONG_PERFSCHEMA_USAGE, MYF(0));
      break;
    default:
     handler::print_error(error, errflag);
     break;
  }
}


/* Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.

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
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef TABLE_EWS_BY_THREAD_BY_EVENT_NAME_H
#define TABLE_EWS_BY_THREAD_BY_EVENT_NAME_H

/**
  @file storage/perfschema/table_ews_by_thread_by_event_name.h
  Table EVENTS_WAITS_SUMMARY_BY_THREAD_BY_EVENT_NAME (declarations).
*/

#include "pfs_column_types.h"
#include "pfs_engine_table.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "table_helper.h"

/**
  @addtogroup Performance_schema_tables
  @{
*/

/**
  A row of table
  PERFORMANCE_SCHEMA.EVENTS_WAITS_SUMMARY_BY_THREAD_BY_EVENT_NAME.
*/
struct row_ews_by_thread_by_event_name
{
  /** Column THREAD_ID. */
  ulonglong m_thread_internal_id;
  /** Column EVENT_NAME. */
  PFS_event_name_row m_event_name;
  /** Columns COUNT_STAR, SUM/MIN/AVG/MAX TIMER_WAIT. */
  PFS_stat_row m_stat;
};

/**
  Position of a cursor on
  PERFORMANCE_SCHEMA.EVENTS_WAITS_SUMMARY_BY_THREAD_BY_EVENT_NAME.
  Index 1 on thread (0 based)
  Index 2 on instrument view
  Index 3 on instrument class (1 based)
*/
struct pos_ews_by_thread_by_event_name
: public PFS_triple_index, public PFS_instrument_view_constants
{
  pos_ews_by_thread_by_event_name()
    : PFS_triple_index(0, FIRST_VIEW, 1)
  {}

  inline void reset(void)
  {
    m_index_1= 0;
    m_index_2= FIRST_VIEW;
    m_index_3= 1;
  }

  inline bool has_more_thread(void)
  { return (m_index_1 < thread_max); }

  inline void next_thread(void)
  {
    m_index_1++;
    m_index_2= FIRST_VIEW;
    m_index_3= 1;
  }

  inline bool has_more_view(void)
  { return (m_index_2 <= LAST_VIEW); }

  inline void next_view(void)
  {
    m_index_2++;
    m_index_3= 1;
  }
};

/** Table PERFORMANCE_SCHEMA.EVENTS_WAITS_SUMMARY_BY_THREAD_BY_EVENT_NAME. */
class table_ews_by_thread_by_event_name : public PFS_engine_table
{
public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table* create();
  static int delete_all_rows();

  virtual int rnd_next();
  virtual int rnd_pos(const void *pos);
  virtual void reset_position(void);

protected:
  virtual int read_row_values(TABLE *table,
                              unsigned char *buf,
                              Field **fields,
                              bool read_all);

  table_ews_by_thread_by_event_name();

public:
  ~table_ews_by_thread_by_event_name() = default;

protected:
  void make_row(PFS_thread *thread, PFS_instr_class *klass);

private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Current row. */
  row_ews_by_thread_by_event_name m_row;
  /** True is the current row exists. */
  bool m_row_exists;
  /** Current position. */
  pos_ews_by_thread_by_event_name m_pos;
  /** Next position. */
  pos_ews_by_thread_by_event_name m_next_pos;
};

/** @} */
#endif

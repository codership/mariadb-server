/* Copyright (c) 2010, 2012, Oracle and/or its affiliates. All rights reserved.

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
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef TABLE_ESGS_BY_USER_BY_EVENT_NAME_H
#define TABLE_ESGS_BY_USER_BY_EVENT_NAME_H

/**
  @file storage/perfschema/table_esgs_by_user_by_event_name.h
  Table EVENTS_STAGES_SUMMARY_BY_USER_BY_EVENT_NAME (declarations).
*/

#include "pfs_column_types.h"
#include "pfs_engine_table.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "pfs_user.h"
#include "table_helper.h"

/**
  @addtogroup Performance_schema_tables
  @{
*/

/**
  A row of table
  PERFORMANCE_SCHEMA.EVENTS_STAGES_SUMMARY_BY_USER_BY_EVENT_NAME.
*/
struct row_esgs_by_user_by_event_name
{
  /** Column USER. */
  PFS_user_row m_user;
  /** Column EVENT_NAME. */
  PFS_event_name_row m_event_name;
  /** Columns COUNT_STAR, SUM/MIN/AVG/MAX TIMER_WAIT. */
  PFS_stage_stat_row m_stat;
};

/**
  Position of a cursor on
  PERFORMANCE_SCHEMA.EVENTS_STAGES_SUMMARY_BY_USER_BY_EVENT_NAME.
  Index 1 on user (0 based)
  Index 2 on stage class (1 based)
*/
struct pos_esgs_by_user_by_event_name
: public PFS_double_index
{
  pos_esgs_by_user_by_event_name()
    : PFS_double_index(0, 1)
  {}

  inline void reset(void)
  {
    m_index_1= 0;
    m_index_2= 1;
  }

  inline bool has_more_user(void)
  { return (m_index_1 < user_max); }

  inline void next_user(void)
  {
    m_index_1++;
    m_index_2= 1;
  }

  inline void next_stage(void)
  {
    m_index_2++;
  }
};

/** Table PERFORMANCE_SCHEMA.EVENTS_STAGES_SUMMARY_BY_USER_BY_EVENT_NAME. */
class table_esgs_by_user_by_event_name : public PFS_engine_table
{
public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table* create();
  static int delete_all_rows();

  virtual int rnd_init(bool scan);
  virtual int rnd_next();
  virtual int rnd_pos(const void *pos);
  virtual void reset_position(void);

protected:
  virtual int read_row_values(TABLE *table,
                              unsigned char *buf,
                              Field **fields,
                              bool read_all);

  table_esgs_by_user_by_event_name();

public:
  ~table_esgs_by_user_by_event_name() = default;

protected:
  void make_row(PFS_user *user, PFS_stage_class *klass);

private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Current row. */
  row_esgs_by_user_by_event_name m_row;
  /** True is the current row exists. */
  bool m_row_exists;
  /** Current position. */
  pos_esgs_by_user_by_event_name m_pos;
  /** Next position. */
  pos_esgs_by_user_by_event_name m_next_pos;
};

/** @} */
#endif

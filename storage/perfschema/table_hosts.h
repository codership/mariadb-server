/* Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.

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

#ifndef TABLE_HOSTS_H
#define TABLE_HOSTS_H

#include "pfs_column_types.h"
#include "cursor_by_host.h"
#include "table_helper.h"

struct PFS_host;

/**
  \addtogroup Performance_schema_tables
  @{
*/

/**
  A row of PERFORMANCE_SCHEMA.HOSTS.
*/
struct row_hosts
{
  /** Column HOST. */
  PFS_host_row m_host;
  /** Columns CURRENT_CONNECTIONS, TOTAL_CONNECTIONS. */
  PFS_connection_stat_row m_connection_stat;
};

/** Table PERFORMANCE_SCHEMA.THREADS. */
class table_hosts : public cursor_by_host
{
public:
  /** Table share */
  static PFS_engine_table_share m_share;
  /** Table builder */
  static PFS_engine_table* create();
  static int delete_all_rows();

protected:
  virtual int read_row_values(TABLE *table,
                              unsigned char *buf,
                              Field **fields,
                              bool read_all);


protected:
  table_hosts();

public:
  ~table_hosts() = default;

private:
  virtual void make_row(PFS_host *pfs);

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Current row. */
  row_hosts m_row;
  /** True if the current row exists. */
  bool m_row_exists;
};

/** @} */
#endif

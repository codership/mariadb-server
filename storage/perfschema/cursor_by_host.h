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
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef CURSOR_BY_HOST_H
#define CURSOR_BY_HOST_H

/**
  @file storage/perfschema/cursor_by_host.h
  Cursor CURSOR_BY_HOST (declarations).
*/

#include "pfs_engine_table.h"
#include "pfs_host.h"
#include "table_helper.h"

/**
  @addtogroup Performance_schema_tables
  @{
*/

/** Cursor CURSOR_BY_HOST. */
class cursor_by_host : public PFS_engine_table
{
public:
  virtual int rnd_next();
  virtual int rnd_pos(const void *pos);
  virtual void reset_position(void);

protected:
  cursor_by_host(const PFS_engine_table_share *share);

public:
  ~cursor_by_host() = default;

protected:
  virtual void make_row(PFS_host *host)= 0;

private:
  /** Current position. */
  PFS_simple_index m_pos;
  /** Next position. */
  PFS_simple_index m_next_pos;
};

/** @} */
#endif

# Copyright (c) 2009, 2019, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/sql
                    ${CMAKE_BINARY_DIR}/sql
                    ${PCRE_INCLUDES}
                    ${SSL_INCLUDE_DIRS})

ADD_DEFINITIONS(-DMYSQL_SERVER)

#
# Maintainer: keep this list sorted, to avoid merge collisions.
# Tip: ls -1 *.h, ls -1 *.cc
#
SET(PERFSCHEMA_SOURCES
ha_perfschema.h
cursor_by_account.h
cursor_by_host.h
cursor_by_thread.h
cursor_by_user.h
pfs.h
pfs_account.h
pfs_atomic.h
pfs_column_types.h
pfs_column_values.h
pfs_con_slice.h
pfs_defaults.h
pfs_digest.h
pfs_engine_table.h
pfs_events.h
pfs_events_stages.h
pfs_events_statements.h
pfs_events_waits.h
pfs_global.h
pfs_host.h
pfs_instr.h
pfs_instr_class.h
pfs_lock.h
pfs_server.h
pfs_setup_actor.h
pfs_setup_object.h
pfs_stat.h
pfs_timer.h
pfs_user.h
pfs_visitor.h
table_accounts.h
table_all_instr.h
table_esgs_by_account_by_event_name.h
table_esgs_by_host_by_event_name.h
table_esgs_by_thread_by_event_name.h
table_esgs_by_user_by_event_name.h
table_esgs_global_by_event_name.h
table_esms_by_account_by_event_name.h
table_esms_by_host_by_event_name.h
table_esms_by_digest.h
table_esms_by_thread_by_event_name.h
table_esms_by_user_by_event_name.h
table_esms_global_by_event_name.h
table_events_stages.h
table_events_statements.h
table_events_waits.h
table_events_waits_summary.h
table_ews_by_account_by_event_name.h
table_ews_by_host_by_event_name.h
table_ews_by_thread_by_event_name.h
table_ews_by_user_by_event_name.h
table_ews_global_by_event_name.h
table_file_instances.h
table_file_summary_by_instance.h
table_file_summary_by_event_name.h
table_socket_instances.h
table_socket_summary_by_instance.h
table_socket_summary_by_event_name.h
table_helper.h
table_host_cache.h
table_hosts.h
table_os_global_by_type.h
table_performance_timers.h
table_setup_actors.h
table_setup_consumers.h
table_setup_instruments.h
table_setup_objects.h
table_setup_timers.h
table_sync_instances.h
table_threads.h
table_tiws_by_index_usage.h
table_tiws_by_table.h
table_tlws_by_table.h
table_users.h
cursor_by_thread_connect_attr.h
table_session_connect.h
table_session_connect_attrs.h
table_session_account_connect_attrs.h
cursor_by_account.cc
cursor_by_host.cc
cursor_by_thread.cc
cursor_by_user.cc
ha_perfschema.cc
pfs.cc
pfs_account.cc
pfs_autosize.cc
pfs_column_values.cc
pfs_con_slice.cc
pfs_defaults.cc
pfs_digest.cc
pfs_engine_table.cc
pfs_events_stages.cc
pfs_events_statements.cc
pfs_events_waits.cc
pfs_global.cc
pfs_host.cc
pfs_instr.cc
pfs_instr_class.cc
pfs_server.cc
pfs_setup_actor.cc
pfs_setup_object.cc
pfs_timer.cc
pfs_user.cc
pfs_visitor.cc
table_accounts.cc
table_all_instr.cc
table_esgs_by_account_by_event_name.cc
table_esgs_by_host_by_event_name.cc
table_esgs_by_thread_by_event_name.cc
table_esgs_by_user_by_event_name.cc
table_esgs_global_by_event_name.cc
table_esms_by_account_by_event_name.cc
table_esms_by_host_by_event_name.cc
table_esms_by_digest.cc
table_esms_by_thread_by_event_name.cc
table_esms_by_user_by_event_name.cc
table_esms_global_by_event_name.cc
table_events_stages.cc
table_events_statements.cc
table_events_waits.cc
table_events_waits_summary.cc
table_ews_by_account_by_event_name.cc
table_ews_by_host_by_event_name.cc
table_ews_by_thread_by_event_name.cc
table_ews_by_user_by_event_name.cc
table_ews_global_by_event_name.cc
table_file_instances.cc
table_file_summary_by_instance.cc
table_file_summary_by_event_name.cc
table_socket_instances.cc
table_socket_summary_by_instance.cc
table_socket_summary_by_event_name.cc
table_helper.cc
table_host_cache.cc
table_hosts.cc
table_os_global_by_type.cc
table_performance_timers.cc
table_setup_actors.cc
table_setup_consumers.cc
table_setup_instruments.cc
table_setup_objects.cc
table_setup_timers.cc
table_sync_instances.cc
table_threads.cc
table_tiws_by_index_usage.cc
table_tiws_by_table.cc
table_tlws_by_table.cc
table_users.cc
cursor_by_thread_connect_attr.cc
table_session_connect.cc
table_session_connect_attrs.cc
table_session_account_connect_attrs.cc
)

MYSQL_ADD_PLUGIN(perfschema ${PERFSCHEMA_SOURCES} STORAGE_ENGINE DEFAULT
  STATIC_ONLY DEPENDS GenServerSource)

IF (TARGET perfschema)
  IF(WITH_UNIT_TESTS)
    ADD_SUBDIRECTORY(unittest)
  ENDIF(WITH_UNIT_TESTS)
ENDIF(TARGET perfschema)

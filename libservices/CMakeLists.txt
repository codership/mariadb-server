# Copyright (c) 2006 MySQL AB, 2010 Oracle and/or its affiliates.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335 USA

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include)

SET(MYSQLSERVICES_SOURCES
  base64_service.c
  debug_sync_service.c
  encryption_scheme_service.c
  encryption_service.c
  kill_statement_service.c
  logger_service.c
  my_crypt_service.c
  my_md5_service.c
  my_print_error_service.c
  my_sha1_service.c
  my_sha2_service.c
  my_snprintf_service.c
  progress_report_service.c
  thd_alloc_service.c
  thd_autoinc_service.c
  thd_error_context_service.c
  thd_rnd_service.c
  thd_specifics_service.c
  thd_timezone_service.c
  thd_wait_service.c
  wsrep_service.c
  json_service.c
  sql_service.c
  )

ADD_CONVENIENCE_LIBRARY(mysqlservices ${MYSQLSERVICES_SOURCES})
INSTALL(TARGETS mysqlservices DESTINATION ${INSTALL_LIBDIR} COMPONENT Development)

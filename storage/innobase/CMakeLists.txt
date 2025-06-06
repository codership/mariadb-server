# Copyright (c) 2006, 2017, Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2014, 2022, MariaDB Corporation.
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

# This is the CMakeLists for InnoDB

INCLUDE(CheckFunctionExists)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCSourceRuns)
INCLUDE(lz4.cmake)
INCLUDE(lzo.cmake)
INCLUDE(lzma.cmake)
INCLUDE(bzip2.cmake)
INCLUDE(snappy.cmake)
INCLUDE(numa)
INCLUDE(TestBigEndian)

MYSQL_CHECK_LZ4()
MYSQL_CHECK_LZO()
MYSQL_CHECK_LZMA()
MYSQL_CHECK_BZIP2()
MYSQL_CHECK_SNAPPY()
MYSQL_CHECK_NUMA()

INCLUDE(${MYSQL_CMAKE_SCRIPT_DIR}/compile_flags.cmake)

IF(CMAKE_CROSSCOMPILING)
  # Use CHECK_C_SOURCE_COMPILES instead of CHECK_C_SOURCE_RUNS when
  # cross-compiling. Not as precise, but usually good enough.
  # This only make sense for atomic tests in this file, this trick doesn't
  # work in a general case.
  MACRO(CHECK_C_SOURCE SOURCE VAR)
    CHECK_C_SOURCE_COMPILES("${SOURCE}" "${VAR}")
  ENDMACRO()
ELSE()
  MACRO(CHECK_C_SOURCE SOURCE VAR)
    CHECK_C_SOURCE_RUNS("${SOURCE}" "${VAR}")
  ENDMACRO()
ENDIF()

# OS tests
IF(UNIX)
  IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    ADD_DEFINITIONS("-D_GNU_SOURCE=1")

    CHECK_INCLUDE_FILES (libaio.h HAVE_LIBAIO_H)
    CHECK_LIBRARY_EXISTS(aio io_queue_init "" HAVE_LIBAIO)

    IF(HAVE_LIBAIO_H AND HAVE_LIBAIO)
      ADD_DEFINITIONS(-DLINUX_NATIVE_AIO=1)
      LINK_LIBRARIES(aio)
    ENDIF()
    IF(HAVE_LIBNUMA)
      LINK_LIBRARIES(numa)
    ENDIF()
  ENDIF()
ENDIF()

SET(MUTEXTYPE "event" CACHE STRING "Mutex type: event, sys or futex")

IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
# After: WL#5825 Using C++ Standard Library with MySQL code
#       we no longer use -fno-exceptions
#	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

# Add -Wconversion if compiling with GCC
## As of Mar 15 2011 this flag causes 3573+ warnings. If you are reading this
## please fix them and enable the following code:
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion")

  IF (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR
      CMAKE_SYSTEM_PROCESSOR MATCHES "i386" AND
      CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.6)
    INCLUDE(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-fno-builtin-memcmp" HAVE_NO_BUILTIN_MEMCMP)
    IF (HAVE_NO_BUILTIN_MEMCMP)
      # Work around http://gcc.gnu.org/bugzilla/show_bug.cgi?id=43052
      SET_SOURCE_FILES_PROPERTIES(${CMAKE_CURRENT_SOURCE_DIR}/rem/rem0cmp.cc
	PROPERTIES COMPILE_FLAGS -fno-builtin-memcmp)
    ENDIF()
  ENDIF()
ENDIF()

# Enable InnoDB's UNIV_DEBUG in debug builds
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DUNIV_DEBUG")

OPTION(WITH_INNODB_AHI "Include innodb_adaptive_hash_index" ON)
OPTION(WITH_INNODB_ROOT_GUESS "Cache index root block descriptors" ON)
IF(WITH_INNODB_AHI)
  ADD_DEFINITIONS(-DBTR_CUR_HASH_ADAPT -DBTR_CUR_ADAPT)
  IF(NOT WITH_INNODB_ROOT_GUESS)
    MESSAGE(WARNING "WITH_INNODB_AHI implies WITH_INNODB_ROOT_GUESS")
    SET(WITH_INNODB_ROOT_GUESS ON)
  ENDIF()
ELSEIF(WITH_INNODB_ROOT_GUESS)
  ADD_DEFINITIONS(-DBTR_CUR_ADAPT)
ENDIF()
ADD_FEATURE_INFO(INNODB_AHI WITH_INNODB_AHI "InnoDB Adaptive Hash Index")
ADD_FEATURE_INFO(INNODB_ROOT_GUESS WITH_INNODB_ROOT_GUESS
                 "Cache index root block descriptors in InnoDB")

OPTION(WITH_INNODB_EXTRA_DEBUG "Enable extra InnoDB debug checks" OFF)
IF(WITH_INNODB_EXTRA_DEBUG)
  ADD_DEFINITIONS(-DUNIV_ZIP_DEBUG)
ENDIF()
ADD_FEATURE_INFO(INNODB_EXTRA_DEBUG WITH_INNODB_EXTRA_DEBUG "Extra InnoDB debug checks")

CHECK_FUNCTION_EXISTS(nanosleep HAVE_NANOSLEEP)
IF(HAVE_NANOSLEEP)
 ADD_DEFINITIONS(-DHAVE_NANOSLEEP=1)
ENDIF()

IF(HAVE_FALLOC_PUNCH_HOLE_AND_KEEP_SIZE)
 ADD_DEFINITIONS(-DHAVE_FALLOC_PUNCH_HOLE_AND_KEEP_SIZE=1)
ENDIF()

IF(NOT MSVC)
  # Work around MDEV-18417, MDEV-18656, MDEV-18417
  IF(WITH_ASAN AND CMAKE_COMPILER_IS_GNUCC AND
     CMAKE_C_COMPILER_VERSION VERSION_LESS "6.0.0")
    SET_SOURCE_FILES_PROPERTIES(trx/trx0rec.cc PROPERTIES COMPILE_FLAGS -O1)
  ENDIF()
ENDIF(NOT MSVC)

CHECK_FUNCTION_EXISTS(vasprintf  HAVE_VASPRINTF)

SET(MUTEXTYPE "event" CACHE STRING "Mutex type: event, sys or futex")

IF(MUTEXTYPE MATCHES "event")
  ADD_DEFINITIONS(-DMUTEX_EVENT)
ELSEIF(MUTEXTYPE MATCHES "futex" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  ADD_DEFINITIONS(-DMUTEX_FUTEX)
ELSE()
   ADD_DEFINITIONS(-DMUTEX_SYS)
ENDIF()


# Include directories under innobase
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/storage/innobase/include
		    ${CMAKE_SOURCE_DIR}/storage/innobase/handler)

# Sun Studio bug with -xO2
IF(CMAKE_CXX_COMPILER_ID MATCHES "SunPro"
	AND CMAKE_CXX_FLAGS_RELEASE MATCHES "O2"
	AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	# Sun Studio 12 crashes with -xO2 flag, but not with higher optimization
	# -xO3
	SET_SOURCE_FILES_PROPERTIES(${CMAKE_CURRENT_SOURCE_DIR}/rem/rem0rec.cc
    PROPERTIES COMPILE_FLAGS -xO3)
ENDIF()

# Avoid generating Hardware Capabilities due to crc32 instructions
IF(CMAKE_SYSTEM_NAME MATCHES "SunOS" AND CMAKE_SYSTEM_PROCESSOR MATCHES "i386")
  MY_CHECK_CXX_COMPILER_FLAG("-Wa,-nH")
  IF(have_CXX__Wa__nH)
    ADD_COMPILE_FLAGS(
      ut/ut0crc32.cc
      COMPILE_FLAGS "-Wa,-nH"
    )
  ENDIF()
ENDIF()

IF(MSVC)
  # Avoid "unreferenced label" warning in generated file
  GET_FILENAME_COMPONENT(_SRC_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)
  SET_SOURCE_FILES_PROPERTIES(${_SRC_DIR}/pars/pars0grm.c
          PROPERTIES COMPILE_FLAGS "/wd4102")
  SET_SOURCE_FILES_PROPERTIES(${_SRC_DIR}/pars/lexyy.c
          PROPERTIES COMPILE_FLAGS "/wd4003")
ENDIF()

# Include directories under innobase
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/storage/innobase/include
                    ${CMAKE_SOURCE_DIR}/storage/innobase/handler
                    ${CMAKE_SOURCE_DIR}/libbinlogevents/include)

SET(INNOBASE_SOURCES
	btr/btr0btr.cc
	btr/btr0bulk.cc
	btr/btr0cur.cc
	btr/btr0pcur.cc
	btr/btr0scrub.cc
	btr/btr0sea.cc
	btr/btr0defragment.cc
	buf/buf0block_hint.cc
	buf/buf0buddy.cc
	buf/buf0buf.cc
	buf/buf0dblwr.cc
	buf/buf0checksum.cc
	buf/buf0dump.cc
	buf/buf0flu.cc
	buf/buf0lru.cc
	buf/buf0rea.cc
	data/data0data.cc
	data/data0type.cc
	dict/dict0boot.cc
	dict/dict0crea.cc
	dict/dict0dict.cc
	dict/dict0load.cc
	dict/dict0mem.cc
	dict/dict0stats.cc
	dict/dict0stats_bg.cc
	dict/dict0defrag_bg.cc
	eval/eval0eval.cc
	eval/eval0proc.cc
	fil/fil0fil.cc
	fil/fil0pagecompress.cc
	fil/fil0crypt.cc
	fsp/fsp0fsp.cc
	fsp/fsp0file.cc
	fsp/fsp0space.cc
	fsp/fsp0sysspace.cc
	fut/fut0lst.cc
	ha/ha0ha.cc
	ha/ha0storage.cc
	ha/hash0hash.cc
	fts/fts0fts.cc
	fts/fts0ast.cc
	fts/fts0blex.cc
	fts/fts0config.cc
	fts/fts0opt.cc
	fts/fts0pars.cc
	fts/fts0que.cc
	fts/fts0sql.cc
	fts/fts0tlex.cc
	gis/gis0geo.cc
	gis/gis0rtree.cc
	gis/gis0sea.cc
	fts/fts0plugin.cc
	handler/ha_innodb.cc
	handler/handler0alter.cc
	handler/i_s.cc
	ibuf/ibuf0ibuf.cc
	lock/lock0iter.cc
	lock/lock0prdt.cc
	lock/lock0lock.cc
	lock/lock0wait.cc
	log/log0log.cc
	log/log0recv.cc
	log/log0crypt.cc
	mach/mach0data.cc
	mem/mem0mem.cc
	mtr/mtr0log.cc
	mtr/mtr0mtr.cc
	os/os0file.cc
	os/os0proc.cc
	os/os0event.cc
	os/os0thread.cc
	page/page0cur.cc
	page/page0page.cc
	page/page0zip.cc
	pars/lexyy.cc
	pars/pars0grm.cc
	pars/pars0opt.cc
	pars/pars0pars.cc
	pars/pars0sym.cc
	que/que0que.cc
	read/read0read.cc
	rem/rem0cmp.cc
	rem/rem0rec.cc
	row/row0ext.cc
	row/row0ftsort.cc
	row/row0import.cc
	row/row0ins.cc
	row/row0merge.cc
	row/row0mysql.cc
	row/row0log.cc
	row/row0purge.cc
	row/row0row.cc
	row/row0sel.cc
	row/row0uins.cc
	row/row0umod.cc
	row/row0undo.cc
	row/row0upd.cc
	row/row0quiesce.cc
	row/row0vers.cc
	srv/srv0conc.cc
	srv/srv0mon.cc
	srv/srv0srv.cc
	srv/srv0start.cc
	sync/sync0arr.cc
	sync/sync0rw.cc
	sync/sync0debug.cc
	sync/sync0sync.cc
	trx/trx0i_s.cc
	trx/trx0purge.cc
	trx/trx0rec.cc
	trx/trx0roll.cc
	trx/trx0rseg.cc
	trx/trx0sys.cc
	trx/trx0trx.cc
	trx/trx0undo.cc
	ut/ut0crc32.cc
	ut/ut0dbg.cc
	ut/ut0list.cc
	ut/ut0mem.cc
	ut/ut0new.cc
	ut/ut0rbt.cc
	ut/ut0rnd.cc
	ut/ut0ut.cc
	ut/ut0vec.cc
	ut/ut0wqueue.cc)

MYSQL_ADD_PLUGIN(innobase ${INNOBASE_SOURCES} STORAGE_ENGINE
  MODULE_OUTPUT_NAME ha_innodb
  DEFAULT RECOMPILE_FOR_EMBEDDED
  LINK_LIBRARIES
	${ZLIB_LIBRARY}
	${CRC32_LIBRARY}
	${NUMA_LIBRARY}
	${LIBSYSTEMD}
	${LINKER_SCRIPT})

IF(NOT TARGET innobase)
  RETURN()
ENDIF()

ADD_DEFINITIONS(${SSL_DEFINES})

# A GCC bug causes crash when compiling these files on ARM64 with -O1+
# Compile them with -O0 as a workaround.
IF(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"
   AND CMAKE_C_COMPILER_VERSION VERSION_LESS "5.2.0")
  ADD_COMPILE_FLAGS(
      btr/btr0btr.cc
      btr/btr0cur.cc
      buf/buf0buf.cc
      fts/fts0fts.cc
      gis/gis0sea.cc
      handler/handler0alter.cc
      mtr/mtr0mtr.cc
      row/row0merge.cc
      row/row0mysql.cc
      srv/srv0srv.cc
      COMPILE_FLAGS "-O0"
      )
ENDIF()
IF(MSVC)
  IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
   ADD_COMPILE_FLAGS(
      pars/lexyy.cc
      COMPILE_FLAGS "/wd4267")
  ENDIF()
  # silence "switch statement contains 'default' but no 'case' label
  # on generated file.
  TARGET_COMPILE_OPTIONS(innobase PRIVATE "/wd4065")
ENDIF()

IF(NOT (PLUGIN_INNOBASE STREQUAL DYNAMIC))
  ADD_SUBDIRECTORY(${CMAKE_SOURCE_DIR}/extra/mariabackup ${CMAKE_BINARY_DIR}/extra/mariabackup)
ENDIF()

IF(WITH_UNIT_TESTS)
  ADD_SUBDIRECTORY(unittest)
ENDIF()

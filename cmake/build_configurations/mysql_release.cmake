# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2011, 2022, MariaDB Corporation.
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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335  USA 

# This file includes build settings used for MySQL release

INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckTypeSize)

# XXX package_name.cmake uses this too, move it somewhere global
CHECK_TYPE_SIZE("void *" SIZEOF_VOIDP)
IF(SIZEOF_VOIDP EQUAL 4)
  SET(32BIT 1)
ENDIF()
IF(SIZEOF_VOIDP EQUAL 8)
  SET(64BIT 1)
ENDIF()

# include aws_key_management plugin in release builds
OPTION(AWS_SDK_EXTERNAL_PROJECT  "Allow download and build AWS C++ SDK" ON)

SET(FEATURE_SET "community" CACHE STRING 
" Selection of features. Options are
 - xsmall : 
 - small: embedded
 - classic: embedded + archive + federated + blackhole 
 - large :  embedded + archive + federated + blackhole + innodb
 - xlarge:  embedded + archive + federated + blackhole + innodb + partition
 - community:  all  features (currently == xlarge)
"
)

SET(FEATURE_SET_xsmall  1)
SET(FEATURE_SET_small   2)
SET(FEATURE_SET_classic 3)
SET(FEATURE_SET_large   5)
SET(FEATURE_SET_xlarge  6)
SET(FEATURE_SET_community 7)

IF(FEATURE_SET)
  STRING(TOLOWER ${FEATURE_SET} feature_set)
  SET(num ${FEATURE_SET_${feature_set}})
  IF(NOT num)
   MESSAGE(FATAL_ERROR "Invalid FEATURE_SET option '${feature_set}'. 
   Should be xsmall, small, classic, large, or community
   ")
  ENDIF()
  SET(PLUGIN_PARTITION "NO")
  IF(num EQUAL FEATURE_SET_xsmall)
    SET(WITH_NONE ON)
  ENDIF()
  
  IF(num GREATER FEATURE_SET_xsmall AND NOT WIN32)
    SET(WITH_EMBEDDED_SERVER ON CACHE BOOL "")
  ENDIF()
  IF(num GREATER FEATURE_SET_small)
    SET(PLUGIN_ARCHIVE "STATIC")
    SET(PLUGIN_BLACKHOLE "STATIC")
    SET(PLUGIN_FEDERATEDX "STATIC")
    SET(PLUGIN_FEEDBACK "STATIC")
  ENDIF()
  IF(num GREATER FEATURE_SET_classic)
    SET(PLUGIN_INNOBASE "STATIC")
  ENDIF()
  IF(num GREATER FEATURE_SET_large)
    SET(PLUGIN_PARTITION "STATIC")
    #SET(PLUGIN_CASSANDRA "STATIC")
  ENDIF()
  IF(num GREATER FEATURE_SET_xlarge)
   # OPTION(WITH_ALL ON) 
   # better no set this, otherwise server would be linked 
   # statically with experimental stuff like audit_null
  ENDIF()
ENDIF()

SET(WITH_INNODB_SNAPPY OFF CACHE STRING "")
SET(WITH_NUMA 0 CACHE BOOL "")
SET(CPU_LEVEL1_DCACHE_LINESIZE 0)

IF(NOT EXISTS ${CMAKE_SOURCE_DIR}/.git)
  SET(GIT_EXECUTABLE GIT_EXECUTABLE-NOTFOUND CACHE FILEPATH "")
ENDIF()

IF(WIN32)
  SET(INSTALL_MYSQLTESTDIR "" CACHE STRING "")
  SET(INSTALL_SQLBENCHDIR  "" CACHE STRING "")
  SET(INSTALL_SUPPORTFILESDIR ""  CACHE STRING "")
ELSEIF(RPM)
  SET(WITH_SSL system CACHE STRING "")
  SET(WITH_ZLIB system CACHE STRING "")
  SET(CHECKMODULE /usr/bin/checkmodule CACHE FILEPATH "")
  SET(SEMODULE_PACKAGE /usr/bin/semodule_package CACHE FILEPATH "")
  SET(WITH_JEMALLOC "yes" CACHE STRING "")
  SET(PLUGIN_AUTH_SOCKET YES CACHE STRING "")
  IF(RPM MATCHES "fedora|centos|rhel|rocky|alma")
    SET(WITH_INNODB_BZIP2 OFF CACHE STRING "")
    SET(WITH_INNODB_LZO OFF CACHE STRING "")
    SET(WITH_ROCKSDB_BZip2 OFF CACHE STRING "")
  ENDIF()
  IF(RPM MATCHES "opensuse|sles|centos|rhel|rocky|alma")
    SET(WITH_INNODB_LZ4 OFF CACHE STRING "")
    SET(WITH_ROCKSDB_LZ4 OFF CACHE STRING "")
    SET(GRN_WITH_LZ4 no CACHE STRING "")
  ENDIF()
ELSEIF(DEB)
  SET(WITH_SSL system CACHE STRING "")
  SET(WITH_ZLIB system CACHE STRING "")
  SET(WITH_LIBWRAP ON)
  SET(HAVE_EMBEDDED_PRIVILEGE_CONTROL ON)
  SET(WITH_JEMALLOC "yes" CACHE STRING "")
  SET(PLUGIN_AUTH_SOCKET YES CACHE STRING "")
  SET(WITH_INNODB_BZIP2 OFF CACHE STRING "")
  SET(WITH_INNODB_LZMA OFF CACHE STRING "")
  SET(WITH_INNODB_LZO OFF CACHE STRING "")
  SET(WITH_ROCKSDB_BZip2 OFF CACHE STRING "")
ELSE()
  SET(WITH_SSL bundled CACHE STRING "")
  SET(WITH_PCRE bundled CACHE STRING "")
  SET(WITH_ZLIB bundled CACHE STRING "")
  SET(WITH_JEMALLOC static CACHE STRING "")
  SET(PLUGIN_AUTH_SOCKET STATIC CACHE STRING "")
  SET(WITH_INNODB_BZIP2 OFF CACHE STRING "")
  SET(WITH_INNODB_LZ4 OFF CACHE STRING "")
  SET(WITH_INNODB_LZO OFF CACHE STRING "")
  SET(WITH_ROCKSDB_BZip2 OFF CACHE STRING "")
  SET(WITH_ROCKSDB_LZ4 OFF CACHE STRING "")
  SET(GRN_WITH_LZ4 no CACHE STRING "")
ENDIF()

IF(NOT COMPILATION_COMMENT)
  SET(COMPILATION_COMMENT "MariaDB Server")
ENDIF()

IF(WIN32)
  IF(NOT CMAKE_USING_VC_FREE_TOOLS)
    # Sign executables with authenticode certificate
    SET(SIGNCODE 1 CACHE BOOL "")
  ENDIF()
ENDIF()

IF(UNIX)
  SET(WITH_EXTRA_CHARSETS all CACHE STRING "")
  SET(PLUGIN_AUTH_PAM YES CACHE BOOL "")

  IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    IF(NOT IGNORE_AIO_CHECK)
      # Ensure aio is available on Linux (required by InnoDB)
      CHECK_INCLUDE_FILES(libaio.h HAVE_LIBAIO_H)
      CHECK_LIBRARY_EXISTS(aio io_queue_init "" HAVE_LIBAIO)
      IF(NOT HAVE_LIBAIO_H OR NOT HAVE_LIBAIO)
        UNSET(HAVE_LIBAIO_H CACHE)
        UNSET(HAVE_LIBAIO CACHE)
        MESSAGE(FATAL_ERROR "
        aio is required on Linux, you need to install the required library:

          Debian/Ubuntu:              apt-get install libaio-dev
          RedHat/Fedora/Oracle Linux: yum install libaio-devel
          SuSE:                       zypper install libaio-devel

          If you really do not want it, pass -DIGNORE_AIO_CHECK=ON to cmake.
        ")
      ENDIF()

      # Unfortunately, linking shared libmysqld with static aio
      # does not work,  unless we add also dynamic one. This also means
      # libmysqld.so will depend on libaio.so
      #SET(LIBMYSQLD_SO_EXTRA_LIBS aio)
    ENDIF()
  ENDIF()

ENDIF()

# Compiler options
IF(UNIX)  

  # Default GCC flags
  IF(CMAKE_COMPILER_IS_GNUCC)
    SET(COMMON_C_FLAGS               "-g -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing  -Wno-uninitialized")
    SET(CMAKE_C_FLAGS_DEBUG          "-O ${COMMON_C_FLAGS}")
    SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 ${COMMON_C_FLAGS}")
  ENDIF()
  IF(CMAKE_COMPILER_IS_GNUCXX)
    SET(COMMON_CXX_FLAGS               "-g -static-libgcc -fno-omit-frame-pointer -fno-strict-aliasing -Wno-uninitialized")
    SET(CMAKE_CXX_FLAGS_DEBUG          "-O ${COMMON_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 ${COMMON_CXX_FLAGS}")
  ENDIF()

  # IBM Z flags
  IF(CMAKE_SYSTEM_PROCESSOR MATCHES "s390x")
    IF(RPM MATCHES "(rhel|centos)6" OR RPM MATCHES "(suse|sles)11")
      SET(z_flags "-funroll-loops -march=z9-109 -mtune=z10 ")
    ELSEIF(RPM MATCHES "(rhel|centos)7" OR RPM MATCHES "(suse|sles)12")
      SET(z_flags "-funroll-loops -march=z196 -mtune=zEC12 ")
    ELSE()
      SET(z_flags "")
    ENDIF()

    IF(CMAKE_COMPILER_IS_GNUCC)
      SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${z_flags}${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    ENDIF()
    IF(CMAKE_COMPILER_IS_GNUCXX)
      SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${z_flags}${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    ENDIF()
    UNSET(z_flags)
  ENDIF()

  # HPUX flags
  IF(CMAKE_SYSTEM_NAME MATCHES "HP-UX")
    IF(CMAKE_C_COMPILER_ID MATCHES "HP")
      IF(CMAKE_SYSTEM_PROCESSOR MATCHES "ia64")
        SET(COMMON_C_FLAGS                 "+DSitanium2 -mt -AC99")
        SET(COMMON_CXX_FLAGS               "+DSitanium2 -mt -Aa")
        SET(CMAKE_C_FLAGS_DEBUG            "+O0 -g ${COMMON_C_FLAGS}")
        SET(CMAKE_CXX_FLAGS_DEBUG          "+O0 -g ${COMMON_CXX_FLAGS}")
	# We have seen compiler bugs with optimisation and -g, so disabled for now
        SET(CMAKE_C_FLAGS_RELWITHDEBINFO   "+O2 ${COMMON_C_FLAGS}")
        SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "+O2 ${COMMON_CXX_FLAGS}")
      ENDIF()
    ENDIF()
    SET(WITH_SSL no)
  ENDIF()

  # Linux flags
  IF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    IF(CMAKE_C_COMPILER_ID MATCHES "Intel")
      SET(COMMON_C_FLAGS                 "-static-intel -static-libgcc -g -mp -restrict")
      SET(COMMON_CXX_FLAGS               "-static-intel -static-libgcc -g -mp -restrict -fno-exceptions")
      IF(CMAKE_SYSTEM_PROCESSOR MATCHES "ia64")
        SET(COMMON_C_FLAGS               "${COMMON_C_FLAGS} -no-ftz -no-prefetch")
        SET(COMMON_CXX_FLAGS             "${COMMON_CXX_FLAGS} -no-ftz -no-prefetch")
      ENDIF()
      SET(CMAKE_C_FLAGS_DEBUG            "${COMMON_C_FLAGS}")
      SET(CMAKE_CXX_FLAGS_DEBUG          "${COMMON_CXX_FLAGS}")
      SET(CMAKE_C_FLAGS_RELWITHDEBINFO   "-O3 -unroll2 -ip ${COMMON_C_FLAGS}")
      SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -unroll2 -ip ${COMMON_CXX_FLAGS}")
      SET(WITH_SSL no)
    ENDIF()
  ENDIF()

  # Default Clang flags
  IF(CMAKE_C_COMPILER_ID MATCHES "Clang")
    SET(COMMON_C_FLAGS               "-g -fno-omit-frame-pointer -fno-strict-aliasing")
    SET(CMAKE_C_FLAGS_DEBUG          "${COMMON_C_FLAGS}")
    SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 ${COMMON_C_FLAGS}")
  ENDIF()
  IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    SET(COMMON_CXX_FLAGS               "-g -fno-omit-frame-pointer -fno-strict-aliasing")
    SET(CMAKE_CXX_FLAGS_DEBUG          "${COMMON_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 ${COMMON_CXX_FLAGS}")
  ENDIF()

  # Solaris flags
  IF(CMAKE_SYSTEM_NAME MATCHES "SunOS")
    IF(CMAKE_SYSTEM_VERSION VERSION_GREATER "5.9")
      # Link mysqld with mtmalloc on Solaris 10 and later
      SET(WITH_MYSQLD_LDFLAGS "-lmtmalloc" CACHE STRING "")
    ENDIF()
    IF(CMAKE_C_COMPILER_ID MATCHES "SunPro")
      IF(CMAKE_SYSTEM_PROCESSOR MATCHES "i386")
        SET(COMMON_C_FLAGS                   "-g -mt -fsimple=1 -ftrap=%none -nofstore -xbuiltin=%all -xlibmil -xlibmopt -xtarget=generic")
        SET(COMMON_CXX_FLAGS                 "-g0 -mt -fsimple=1 -ftrap=%none -nofstore -xbuiltin=%all -features=no%except -xlibmil -xlibmopt -xtarget=generic")
        SET(CMAKE_C_FLAGS_DEBUG              "-xO1 ${COMMON_C_FLAGS}")
        SET(CMAKE_CXX_FLAGS_DEBUG            "-xO1 ${COMMON_CXX_FLAGS}")
        IF(32BIT)
          SET(CMAKE_C_FLAGS_RELWITHDEBINFO   "-xO2 ${COMMON_C_FLAGS}")
          SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-xO2 ${COMMON_CXX_FLAGS}")
        ELSEIF(64BIT)
          SET(CMAKE_C_FLAGS_RELWITHDEBINFO   "-xO3 ${COMMON_C_FLAGS}")
          SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-xO3 ${COMMON_CXX_FLAGS}")
        ENDIF()
      ELSE() 
        # Assume !x86 is SPARC
        SET(COMMON_C_FLAGS                 "-g -Xa -xstrconst -mt")
        SET(COMMON_CXX_FLAGS               "-g0 -noex -mt")
        IF(32BIT)
          SET(COMMON_C_FLAGS               "${COMMON_C_FLAGS} -xarch=sparc")
          SET(COMMON_CXX_FLAGS             "${COMMON_CXX_FLAGS} -xarch=sparc")
	ENDIF()
        SET(CMAKE_C_FLAGS_DEBUG            "${COMMON_C_FLAGS}")
        SET(CMAKE_CXX_FLAGS_DEBUG          "${COMMON_CXX_FLAGS}")
        SET(CMAKE_C_FLAGS_RELWITHDEBINFO   "-xO3 ${COMMON_C_FLAGS}")
        SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-xO3 ${COMMON_CXX_FLAGS}")
      ENDIF()
    ENDIF()
  ENDIF()
ENDIF()

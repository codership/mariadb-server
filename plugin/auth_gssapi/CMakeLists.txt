
SET(CPACK_RPM_gssapi-server_PACKAGE_SUMMARY "GSSAPI authentication plugin for MariaDB server" PARENT_SCOPE)
SET(CPACK_RPM_gssapi-server_PACKAGE_DESCRIPTION "The gssapi authentication plugin allows the user to authenticate with services
that use the Generic Security Services Application Program Interface (GSSAPI).
The gssapi authentication plugin is most often used for authenticating with Microsoft Active Directory." PARENT_SCOPE)

IF (WIN32)
 SET(USE_SSPI 1)
ENDIF()

IF(USE_SSPI)
  SET(GSSAPI_LIBS secur32)
  ADD_DEFINITIONS(-DPLUGIN_SSPI)
  SET(GSSAPI_CLIENT sspi_client.cc)
  SET(GSSAPI_SERVER sspi_server.cc)
  SET(GSSAPI_ERRMSG sspi_errmsg.cc)
ELSE()
 SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
 FIND_PACKAGE(GSSAPI)
 SET_PACKAGE_PROPERTIES(GSSAPI PROPERTIES TYPE OPTIONAL)
 IF(GSSAPI_FOUND)
   INCLUDE_DIRECTORIES(${GSSAPI_INCS})
   ADD_DEFINITIONS(-DPLUGIN_GSSAPI)
   SET(GSSAPI_CLIENT gssapi_client.cc)
   SET(GSSAPI_SERVER gssapi_server.cc)
   SET(GSSAPI_ERRMSG gssapi_errmsg.cc)

   IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
     SET_SOURCE_FILES_PROPERTIES(
       ${GSSAPI_CLIENT} ${GSSAPI_SERVER} ${GSSAPI_ERRMSG}
       PROPERTY COMPILE_FLAGS "-Wno-deprecated-declarations")
   ENDIF()
   SET(CMAKE_REQUIRED_INCLUDES ${GSSAPI_INCS})
   SET(CMAKE_REQUIRED_LIBRARIES ${GSSAPI_LIBS})
   INCLUDE(CheckCXXSymbolExists)
   CHECK_CXX_SYMBOL_EXISTS(krb5_xfree "krb5.h" HAVE_KRB5_XFREE)
   IF(HAVE_KRB5_XFREE)
     ADD_DEFINITIONS(-DHAVE_KRB5_XFREE=1)
   ENDIF()

 ELSE()
   # Can't build plugin
   RETURN()
 ENDIF()
ENDIF ()


MYSQL_ADD_PLUGIN(auth_gssapi server_plugin.cc ${GSSAPI_SERVER} ${GSSAPI_ERRMSG}
                 LINK_LIBRARIES ${GSSAPI_LIBS} 
                 COMPONENT gssapi-server
                 MODULE_ONLY)

# disabled in favor of libmariadb/plugins/auth/auth_gssapi_client.c
#
#MYSQL_ADD_PLUGIN(auth_gssapi_client client_plugin.cc ${GSSAPI_CLIENT} ${GSSAPI_ERRMSG}
#                 LINK_LIBRARIES ${GSSAPI_LIBS COMPONENT ClientPlugins}
#                 COMPONENT gssapi-client
#                 CLIENT
#                 MODULE_ONLY)

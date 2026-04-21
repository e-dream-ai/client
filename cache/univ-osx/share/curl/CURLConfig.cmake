get_filename_component(VCPKG_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
#***************************************************************************
#                                  _   _ ____  _
#  Project                     ___| | | |  _ \| |
#                             / __| | | | |_) | |
#                            | (__| |_| |  _ <| |___
#                             \___|\___/|_| \_\_____|
#
# Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution. The terms
# are also available at https://curl.se/docs/copyright.html.
#
# You may opt to use, copy, modify, merge, publish, distribute and/or sell
# copies of the Software, and permit persons to whom the Software is
# furnished to do so, under the terms of the COPYING file.
#
# This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
# KIND, either express or implied.
#
# SPDX-License-Identifier: curl
#
###########################################################################

####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was curl-config.in.cmake                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

option(CURL_USE_PKGCONFIG "Enable pkg-config to detect CURL dependencies. Default: ON"
  "ON")

if(CMAKE_VERSION VERSION_LESS 3.7)
  message(STATUS "CURL: CURL-specific Find modules require "
    "CMake 3.7 or upper, found: ${CMAKE_VERSION}.")
endif()

include(CMakeFindDependencyMacro)

if("ON")
  if("3")
    find_dependency(OpenSSL "3")
  else()
    find_dependency(OpenSSL)
  endif()
  # Define lib duplicate to fixup lib order for GCC binutils ld in static builds
  if(TARGET OpenSSL::Crypto AND NOT TARGET CURL::OpenSSL_Crypto)
    add_library(CURL::OpenSSL_Crypto INTERFACE IMPORTED)
    get_target_property(_curl_libname OpenSSL::Crypto LOCATION)
    set_target_properties(CURL::OpenSSL_Crypto PROPERTIES INTERFACE_LINK_LIBRARIES "${_curl_libname}")
  endif()
endif()
if("ON")
  find_dependency(ZLIB "1")
  # Define lib duplicate to fixup lib order for GCC binutils ld in static builds
  if(TARGET ZLIB::ZLIB AND NOT TARGET CURL::ZLIB)
    add_library(CURL::ZLIB INTERFACE IMPORTED)
    get_target_property(_curl_libname ZLIB::ZLIB LOCATION)
    set_target_properties(CURL::ZLIB PROPERTIES INTERFACE_LINK_LIBRARIES "${_curl_libname}")
  endif()
endif()

set(_curl_cmake_module_path_save ${CMAKE_MODULE_PATH})
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_MODULE_PATH})

set(_curl_libs "")

if("OFF")
  find_dependency(unofficial-brotli CONFIG)
endif()
if("")
  find_dependency(c-ares CONFIG)
endif()
if("")
endif()
if("")
  find_dependency(Libbacktrace MODULE)
  list(APPEND _curl_libs CURL::libbacktrace)
endif()
if("")
endif()
if(NOT "" AND NOT "ON")
endif()
if("OFF")
endif()
if("OFF")
endif()
if("OFF")
endif()
if("")
  find_dependency(Libssh MODULE)
  list(APPEND _curl_libs CURL::libssh)
endif()
if("OFF")
  find_dependency(libssh2 CONFIG)
endif()
if("")
  find_dependency(Libuv MODULE)
  list(APPEND _curl_libs CURL::libuv)
endif()
if("")
  find_dependency(MbedTLS CONFIG)
endif()
if("OFF")
endif()
if("")
  find_dependency(nghttp3 CONFIG)
endif()
if("OFF")
  find_dependency(ngtcp2 CONFIG)
endif()
if("")
endif()
if("OFF")
  find_dependency(Quiche MODULE)
  list(APPEND _curl_libs CURL::quiche)
endif()
if("")
  find_dependency(Rustls MODULE)
  list(APPEND _curl_libs CURL::rustls)
endif()
if("")
  find_dependency(wolfssl CONFIG)
endif()
if("OFF")
  find_dependency(zstd CONFIG)
endif()

set(CMAKE_MODULE_PATH ${_curl_cmake_module_path_save})

# Define lib duplicate to fixup lib order for GCC binutils ld in static builds
if(WIN32 AND NOT TARGET CURL::win32_winsock)
  add_library(CURL::win32_winsock INTERFACE IMPORTED)
  set_target_properties(CURL::win32_winsock PROPERTIES INTERFACE_LINK_LIBRARIES "ws2_32")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CURLTargets.cmake")

# Alias for either shared or static library
if(NOT TARGET CURL::libcurl)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.11 AND CMAKE_VERSION VERSION_LESS 3.18)
    set_target_properties(CURL::libcurl_static PROPERTIES IMPORTED_GLOBAL TRUE)
  endif()
  add_library(CURL::libcurl ALIAS CURL::libcurl_static)
endif()

if(TARGET CURL::libcurl_static)
  # CMake before CMP0099 (CMake 3.17 2020-03-20) did not propagate libdirs to
  # targets. It expected libs to have an absolute filename. As a workaround,
  # manually apply dependency libdirs, for CMake consumers without this policy.
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
    cmake_policy(GET CMP0099 _has_CMP0099)  # https://cmake.org/cmake/help/latest/policy/CMP0099.html
  endif()
  if(NOT _has_CMP0099 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.13 AND _curl_libs)
    set(_curl_libdirs "")
    foreach(_curl_lib IN LISTS _curl_libs)
      if(TARGET "${_curl_lib}")
        get_target_property(_curl_libdir "${_curl_lib}" INTERFACE_LINK_DIRECTORIES)
        if(_curl_libdir)
          list(APPEND _curl_libdirs "${_curl_libdir}")
        endif()
      endif()
    endforeach()
    if(_curl_libdirs)
      target_link_directories(CURL::libcurl_static INTERFACE ${_curl_libdirs})
    endif()
  endif()
endif()

# For compatibility with CMake's FindCURL.cmake
set(CURL_VERSION_STRING "8.19.0")
set(CURL_LIBRARIES CURL::libcurl)
set(CURL_LIBRARIES_PRIVATE "")
# Release usage requirements
set(_z_vcpkg_CURL_CONFIG_LIBS             "-lssl -lcrypto -lz")
set(_z_vcpkg_LIBCURL_PC_LDFLAGS_PRIVATE   "-framework SystemConfiguration -framework CoreFoundation -framework CoreServices -L${VCPKG_IMPORT_PREFIX}/lib")
set(_z_vcpkg_LIBCURL_PC_LIBS_PRIVATE_LIST "-framework SystemConfiguration;-framework CoreFoundation;-framework CoreServices;${VCPKG_IMPORT_PREFIX}/lib/libssl.a;${VCPKG_IMPORT_PREFIX}/lib/libcrypto.a;${VCPKG_IMPORT_PREFIX}/lib/libz.a")
set_and_check(CURL_INCLUDE_DIRS "${PACKAGE_PREFIX_DIR}/include")

set(CURL_SUPPORTED_PROTOCOLS "DICT;FILE;FTP;FTPS;GOPHER;GOPHERS;HTTP;HTTPS;IMAP;IMAPS;IPFS;IPNS;MQTT;MQTTS;POP3;POP3S;RTSP;SMB;SMBS;SMTP;SMTPS;TELNET;TFTP")
set(CURL_SUPPORTED_FEATURES "alt-svc;AsynchDNS;HSTS;HTTPS-proxy;IPv6;Largefile;libz;NTLM;SSL;threadsafe;TLS-SRP;UnixSockets")

foreach(_curl_item IN LISTS CURL_SUPPORTED_PROTOCOLS CURL_SUPPORTED_FEATURES)
  set(CURL_SUPPORTS_${_curl_item} TRUE)
endforeach()

set(_curl_missing_req "")
foreach(_curl_item IN LISTS CURL_FIND_COMPONENTS)
  if(CURL_SUPPORTS_${_curl_item})
    set(CURL_${_curl_item}_FOUND TRUE)
  elseif(CURL_FIND_REQUIRED_${_curl_item})
    list(APPEND _curl_missing_req ${_curl_item})
  endif()
endforeach()

if(_curl_missing_req)
  string(REPLACE ";" " " _curl_missing_req "${_curl_missing_req}")
  if(CURL_FIND_REQUIRED)
    message(FATAL_ERROR "CURL: missing required components: ${_curl_missing_req}")
  endif()
  unset(_curl_missing_req)
endif()

check_required_components("CURL")

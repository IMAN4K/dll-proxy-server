# This is a HTTP/HTTPS forward proxy server implementation
# capable for running inside shared libraries (.dll/.so)
# Copyright (C) 2022 Iman Ahmadvand
#
# This is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

cmake_minimum_required(VERSION 3.14)

project(ProxyServer
    LANGUAGES C CXX
    )

option(BUILD_AS_SHARED_LIB "Build project as shared library" ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/3rdparty)

if (BUILD_AS_SHARED_LIB)
    add_library(${PROJECT_NAME} SHARED)
    add_compile_definitions(BUILD_AS_SHARED_LIB)
else()
    add_executable(${PROJECT_NAME})
endif(BUILD_AS_SHARED_LIB)

target_sources(${PROJECT_NAME} PRIVATE src/main.cpp)

# Qt
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Network REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Network REQUIRED)
target_link_libraries(${PROJECT_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    )

# httpparser
set(HTTP_PARSER_DIR ${THIRD_PARTY_DIR}/httpparser)
file(GLOB_RECURSE hxxFiles ${HTTP_PARSER_DIR}/src/httpparser/*.h)
target_sources(${PROJECT_NAME} PRIVATE ${hxxFiles})
target_include_directories(${PROJECT_NAME} PRIVATE ${HTTP_PARSER_DIR}/src)
if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -Dstrncasecmp=_strnicmp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -Dstrcasecmp=_stricmp)
endif()

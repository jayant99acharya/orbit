# Copyright (c) 2021 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.15)

project(StringManager)
add_library(StringManager STATIC)

target_sources(
  StringManager
  PUBLIC include/StringManager/StringManager.h)

target_sources(
  StringManager
  PRIVATE StringManager.cpp)

target_include_directories(StringManager PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(
  StringManager
  PUBLIC OrbitBase)

add_executable(StringManagerTests)

target_sources(StringManagerTests PRIVATE
        StringManagerTest.cpp)

target_link_libraries(
  StringManagerTests
  PRIVATE StringManager
          GTest::Main
          CONAN_PKG::abseil)

register_test(StringManagerTests)

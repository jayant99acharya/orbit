# Copyright (c) 2021 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.15)

project(SymbolPaths)
add_library(SymbolPaths STATIC)

target_include_directories(SymbolPaths PUBLIC include/)
target_link_libraries(SymbolPaths PUBLIC Qt5::Core)

target_sources(
  SymbolPaths PUBLIC include/SymbolPaths/QSettingsWrapper.h)

target_sources(SymbolPaths PRIVATE QSettingsWrapper.cpp)

add_executable(SymbolPathsTests)
target_sources(SymbolPathsTests PRIVATE QSettingsWrapperTest.cpp)

target_link_libraries(SymbolPathsTests PRIVATE SymbolPaths
                                               GTest::QtCoreMain)

register_test(SymbolPathsTests)

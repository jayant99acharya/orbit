# Copyright (c) 2020 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.15)

project(ObjectUtils)
add_library(ObjectUtils STATIC)

target_sources(
  ObjectUtils
  PUBLIC include/ObjectUtils/Address.h
         include/ObjectUtils/CoffFile.h
         include/ObjectUtils/ElfFile.h
         include/ObjectUtils/PdbFile.h
         include/ObjectUtils/LinuxMap.h)

target_sources(
  ObjectUtils
  PRIVATE
        Address.cpp
        CoffFile.cpp
        ElfFile.cpp
        PdbFile.cpp
        ObjectFile.cpp)

if (NOT WIN32)
target_sources(ObjectUtils PRIVATE LinuxMap.cpp)
endif()

target_include_directories(ObjectUtils PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(
  ObjectUtils
  PUBLIC GrpcProtos
         OrbitBase
         CONAN_PKG::abseil
         CONAN_PKG::llvm-core)

add_executable(ObjectUtilsTests)

target_sources(ObjectUtilsTests PRIVATE
        AddressTest.cpp
        CoffFileTest.cpp
        ElfFileTest.cpp
        ObjectFileTest.cpp
        PdbFileTest.cpp
)

if (NOT WIN32)
target_sources(ObjectUtilsTests PRIVATE LinuxMapTest.cpp)
endif()

target_link_libraries(
  ObjectUtilsTests
  PRIVATE ObjectUtils
          GTest::Main
          CONAN_PKG::llvm-core
          CONAN_PKG::abseil)

register_test(ObjectUtilsTests)

add_fuzzer(ElfFileLoadSymbolsFuzzer ElfFileLoadSymbolsFuzzer.cpp)
target_link_libraries(ElfFileLoadSymbolsFuzzer ObjectUtils)

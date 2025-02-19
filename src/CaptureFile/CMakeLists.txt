# Copyright (c) 2021 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.15)

project(CaptureFile)
add_library(CaptureFile STATIC)

target_sources(
  CaptureFile
  PUBLIC include/CaptureFile/CaptureFile.h
         include/CaptureFile/CaptureFileHelpers.h
         include/CaptureFile/CaptureFileOutputStream.h
         include/CaptureFile/CaptureFileSection.h
         include/CaptureFile/ProtoSectionInputStream.h)

target_sources(
  CaptureFile
  PRIVATE CaptureFileConstants.h
          CaptureFile.cpp
          CaptureFileHelpers.cpp
          CaptureFileOutputStream.cpp
          ProtoSectionInputStreamImpl.cpp
          ProtoSectionInputStreamImpl.h
          FileFragmentInputStream.cpp
          FileFragmentInputStream.h)

target_include_directories(CaptureFile PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(
  CaptureFile
  PUBLIC OrbitBase
         GrpcProtos
         ClientProtos
         CONAN_PKG::protobuf)

add_executable(CaptureFileTests)

target_sources(CaptureFileTests PRIVATE
  CaptureFileHelpersTest.cpp
  CaptureFileOutputStreamTest.cpp
  CaptureFileTest.cpp
  FileFragmentInputStreamTest.cpp
)

target_link_libraries(
  CaptureFileTests
  PRIVATE CaptureFile
          GTest::Main
          CONAN_PKG::abseil)

register_test(CaptureFileTests)

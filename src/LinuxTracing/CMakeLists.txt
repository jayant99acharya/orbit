# Copyright (c) 2020 The Orbit Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.15)

project(LinuxTracing)

add_library(LinuxTracing STATIC)

target_include_directories(LinuxTracing PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include)

target_include_directories(LinuxTracing PRIVATE
        ${CMAKE_CURRENT_LIST_DIR})

target_sources(LinuxTracing PUBLIC
        include/LinuxTracing/Tracer.h)

target_sources(LinuxTracing PRIVATE
        ContextSwitchManager.cpp
        ContextSwitchManager.h
        Function.h
        GpuTracepointVisitor.h
        GpuTracepointVisitor.cpp
        KernelTracepoints.h
        LeafFunctionCallManager.h
        LeafFunctionCallManager.cpp
        LibunwindstackMaps.cpp
        LibunwindstackMaps.h
        LibunwindstackUnwinder.cpp
        LibunwindstackUnwinder.h
        LinuxTracingUtils.h
        LinuxTracingUtils.cpp
        LostAndDiscardedEventVisitor.h
        PerfEvent.cpp
        PerfEvent.h
        PerfEventOpen.cpp
        PerfEventOpen.h
        PerfEventProcessor.cpp
        PerfEventProcessor.h
        PerfEventQueue.cpp
        PerfEventQueue.h
        PerfEventReaders.h
        PerfEventReaders.cpp
        PerfEventRecords.h
        PerfEventRingBuffer.cpp
        PerfEventRingBuffer.h
        PerfEventVisitor.h
        SwitchesStatesNamesVisitor.cpp
        SwitchesStatesNamesVisitor.h
        ThreadStateManager.cpp
        ThreadStateManager.h
        Tracer.cpp
        TracerThread.cpp
        TracerThread.h
        UprobesFunctionCallManager.h
        UprobesReturnAddressManager.h
        UprobesUnwindingVisitor.cpp
        UprobesUnwindingVisitor.h)

target_link_libraries(LinuxTracing PUBLIC
        GrpcProtos
        Introspection
        ObjectUtils
        OrbitBase
        TracingInterface
        CONAN_PKG::abseil
        CONAN_PKG::libunwindstack)

add_executable(LinuxTracingTests)

target_sources(LinuxTracingTests PRIVATE
        ContextSwitchManagerTest.cpp
        GpuTracepointVisitorTest.cpp
        LeafFunctionCallManagerTest.cpp
        LinuxTracingUtilsTest.cpp
        LostAndDiscardedEventVisitorTest.cpp
        PerfEventProcessorTest.cpp
        PerfEventQueueTest.cpp
        ThreadStateManagerTest.cpp
        UprobesFunctionCallManagerTest.cpp
        UprobesReturnAddressManagerTest.cpp
        UprobesUnwindingVisitorTest.cpp)

target_link_libraries(LinuxTracingTests PRIVATE
        LinuxTracing
        TracingInterface
        GTest::GTest
        GTest::Main)

register_test(LinuxTracingTests)

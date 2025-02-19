// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/container/flat_hash_map.h>
#include <absl/strings/str_format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ClientData/CaptureData.h"
#include "ClientData/FunctionUtils.h"
#include "DataViews/AppInterface.h"
#include "DataViews/DataView.h"
#include "DataViews/LiveFunctionsDataView.h"
#include "DataViews/LiveFunctionsInterface.h"
#include "DisplayFormats/DisplayFormats.h"
#include "GrpcProtos/Constants.h"
#include "MetricsUploader/MetricsUploaderStub.h"
#include "MockAppInterface.h"
#include "OrbitBase/File.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/TemporaryFile.h"
#include "OrbitBase/TestUtils.h"
#include "capture.pb.h"
#include "capture_data.pb.h"

using orbit_client_protos::FunctionInfo;
using orbit_client_protos::FunctionStats;
using JumpToTimerMode = orbit_data_views::AppInterface::JumpToTimerMode;
using orbit_client_data::ModuleData;
using orbit_grpc_protos::InstrumentedFunction;
using orbit_grpc_protos::ModuleInfo;

namespace {

constexpr size_t kNumFunctions = 3;
const std::array<uint64_t, kNumFunctions> kFunctionIds{11, 22, 33};
const std::array<std::string, kNumFunctions> kNames{"foo", "main", "ffind"};
const std::array<std::string, kNumFunctions> kPrettyNames{"void foo()", "main(int, char**)",
                                                          "ffind(int)"};
const std::array<std::string, kNumFunctions> kModulePaths{
    "/path/to/foomodule", "/path/to/somemodule", "/path/to/ffindmodule"};
constexpr std::array<uint64_t, kNumFunctions> kAddresses{0x300, 0x100, 0x200};
constexpr std::array<uint64_t, kNumFunctions> kLoadBiases{0x10, 0x20, 0x30};
const std::array<std::string, kNumFunctions> kBuildIds{"build_id_0", "build_id_1", "build_id_2"};

constexpr std::array<uint64_t, kNumFunctions> kCounts{150, 30, 0};
constexpr std::array<uint64_t, kNumFunctions> kTotalTimeNs{450000, 300000, 0};
constexpr std::array<uint64_t, kNumFunctions> kAvgTimeNs{3000, 10000, 0};
constexpr std::array<uint64_t, kNumFunctions> kMinNs{2000, 3000, 0};
constexpr std::array<uint64_t, kNumFunctions> kMaxNs{4000, 12000, 0};
constexpr std::array<uint64_t, kNumFunctions> kStdDevNs{1000, 6000, 0};

constexpr int kColumnSelected = 0;
constexpr int kColumnName = 1;
constexpr int kColumnCount = 2;
constexpr int kColumnTimeTotal = 3;
constexpr int kColumnTimeAvg = 4;
constexpr int kColumnTimeMin = 5;
constexpr int kColumnTimeMax = 6;
constexpr int kColumnStdDev = 7;
constexpr int kColumnModule = 8;
constexpr int kColumnAddress = 9;
constexpr int kNumColumns = 10;

std::string GetExpectedDisplayTime(uint64_t time_ns) {
  return orbit_display_formats::GetDisplayTime(absl::Nanoseconds(time_ns));
}

std::string GetExpectedDisplayAddress(uint64_t address) { return absl::StrFormat("%#x", address); }

std::string GetExpectedDisplayCount(uint64_t count) { return absl::StrFormat("%lu", count); }

std::unique_ptr<orbit_client_data::CaptureData> GenerateTestCaptureData() {
  static orbit_client_data::ModuleManager module_manager{};
  orbit_grpc_protos::CaptureStarted capture_started{};

  for (size_t i = 0; i < kNumFunctions; i++) {
    ModuleInfo module_info{};
    module_info.set_file_path(kModulePaths[i]);
    module_info.set_build_id(kBuildIds[i]);
    module_info.set_load_bias(kLoadBiases[i]);
    (void)module_manager.AddOrUpdateModules({module_info});

    FunctionInfo function;
    function.set_name(kNames[i]);
    function.set_pretty_name(kPrettyNames[i]);
    function.set_module_path(kModulePaths[i]);
    function.set_module_build_id(kBuildIds[i]);
    function.set_address(kAddresses[i]);

    ModuleData* module_data =
        module_manager.GetMutableModuleByPathAndBuildId(kModulePaths[i], kBuildIds[i]);
    module_data->AddFunctionInfoWithBuildId(function, kBuildIds[i]);

    InstrumentedFunction* instrumented_function =
        capture_started.mutable_capture_options()->add_instrumented_functions();
    instrumented_function->set_file_path(function.module_path());
    instrumented_function->set_file_build_id(function.module_build_id());
    instrumented_function->set_file_offset(
        orbit_client_data::function_utils::Offset(function, *module_data));
  }

  auto capture_data = std::make_unique<orbit_client_data::CaptureData>(
      &module_manager, capture_started, std::nullopt, absl::flat_hash_set<uint64_t>{});

  for (size_t i = 0; i < kNumFunctions; i++) {
    FunctionStats stats;
    stats.set_count(kCounts[i]);
    stats.set_total_time_ns(kTotalTimeNs[i]);
    stats.set_average_time_ns(kAvgTimeNs[i]);
    stats.set_min_ns(kMinNs[i]);
    stats.set_max_ns(kMaxNs[i]);
    stats.set_std_dev_ns(kStdDevNs[i]);
    capture_data->AddFunctionStats(kFunctionIds[i], std::move(stats));
  }

  return capture_data;
}

class MockLiveFunctionsInterface : public orbit_data_views::LiveFunctionsInterface {
 public:
  MOCK_METHOD(void, AddIterator, (uint64_t instrumented_function_id, const FunctionInfo* function));
};

class LiveFunctionsDataViewTest : public testing::Test {
 public:
  explicit LiveFunctionsDataViewTest()
      : view_{&live_functions_, &app_, &metrics_uploader_},
        capture_data_(GenerateTestCaptureData()) {
    for (size_t i = 0; i < kNumFunctions; i++) {
      FunctionInfo function;
      function.set_name(kNames[i]);
      function.set_pretty_name(kPrettyNames[i]);
      function.set_module_path(kModulePaths[i]);
      function.set_module_build_id(kBuildIds[i]);
      function.set_address(kAddresses[i]);
      functions_.insert_or_assign(kFunctionIds[i], std::move(function));
    }
  }

  void AddFunctionsByIndices(const std::vector<size_t>& indices) {
    std::set index_set(indices.begin(), indices.end());
    for (size_t index : index_set) {
      CHECK(index < kNumFunctions);
      view_.AddFunction(kFunctionIds[index], functions_.at(kFunctionIds[index]));
    }
  }

 protected:
  MockLiveFunctionsInterface live_functions_;
  orbit_data_views::MockAppInterface app_;
  orbit_metrics_uploader::MetricsUploaderStub metrics_uploader_;
  orbit_data_views::LiveFunctionsDataView view_;

  absl::flat_hash_map<uint64_t, FunctionInfo> functions_;
  std::unique_ptr<orbit_client_data::CaptureData> capture_data_;
};

}  // namespace

TEST_F(LiveFunctionsDataViewTest, ColumnHeadersNotEmpty) {
  EXPECT_GE(view_.GetColumns().size(), 1);
  for (const auto& column : view_.GetColumns()) {
    EXPECT_FALSE(column.header.empty());
  }
}

TEST_F(LiveFunctionsDataViewTest, HasValidDefaultSortingColumn) {
  EXPECT_GE(view_.GetDefaultSortingColumn(), kColumnCount);
  EXPECT_LT(view_.GetDefaultSortingColumn(), view_.GetColumns().size());
}

TEST_F(LiveFunctionsDataViewTest, ColumnValuesAreCorrect) {
  AddFunctionsByIndices({0});

  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));

  // The selected column will be tested separately.
  EXPECT_EQ(view_.GetValue(0, kColumnName), kPrettyNames[0]);
  EXPECT_EQ(view_.GetValue(0, kColumnModule), kModulePaths[0]);
  EXPECT_EQ(view_.GetValue(0, kColumnAddress), GetExpectedDisplayAddress(kAddresses[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnCount), GetExpectedDisplayCount(kCounts[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnTimeTotal), GetExpectedDisplayTime(kTotalTimeNs[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnTimeAvg), GetExpectedDisplayTime(kAvgTimeNs[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnTimeMin), GetExpectedDisplayTime(kMinNs[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnTimeMax), GetExpectedDisplayTime(kMaxNs[0]));
  EXPECT_EQ(view_.GetValue(0, kColumnStdDev), GetExpectedDisplayTime(kStdDevNs[0]));
}

TEST_F(LiveFunctionsDataViewTest, ColumnSelectedShowsRightResults) {
  bool function_selected = false;
  bool frame_track_enabled = false;
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsFunctionSelected).WillRepeatedly(testing::ReturnPointee(&function_selected));
  // The following code guarantees the appearance of frame track icon is determined by
  // frame_track_enable.
  EXPECT_CALL(app_, IsFrameTrackEnabled)
      .WillRepeatedly(testing::ReturnPointee(&frame_track_enabled));
  EXPECT_CALL(app_, HasFrameTrackInCaptureData)
      .WillRepeatedly(testing::ReturnPointee(&frame_track_enabled));

  AddFunctionsByIndices({0});
  EXPECT_EQ(view_.GetValue(0, kColumnSelected), "");

  function_selected = true;
  EXPECT_EQ(view_.GetValue(0, kColumnSelected), "✓");

  function_selected = false;
  frame_track_enabled = true;
  EXPECT_EQ(view_.GetValue(0, kColumnSelected), "F");

  function_selected = true;
  EXPECT_EQ(view_.GetValue(0, kColumnSelected), "✓ F");
}

TEST_F(LiveFunctionsDataViewTest, ContextMenuEntriesArePresentCorrectly) {
  AddFunctionsByIndices({0, 1, 2});
  bool capture_connected;
  std::array<bool, kNumFunctions> functions_selected{false, true, true};
  std::array<bool, kNumFunctions> frame_track_enabled{false, false, true};
  for (size_t i = 0; i < kNumFunctions; i++) {
    if (frame_track_enabled[i]) {
      capture_data_->EnableFrameTrack(kFunctionIds[i]);
    }
  }

  auto get_index_from_function_info = [&](const FunctionInfo& function) -> std::optional<size_t> {
    for (size_t i = 0; i < kNumFunctions; i++) {
      if (kNames[i] == function.name()) return i;
    }
    return std::nullopt;
  };
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsCaptureConnected).WillRepeatedly(testing::ReturnPointee(&capture_connected));
  EXPECT_CALL(app_, IsFunctionSelected).WillRepeatedly([&](const FunctionInfo& function) -> bool {
    std::optional<size_t> index = get_index_from_function_info(function);
    EXPECT_TRUE(index.has_value());
    return functions_selected.at(index.value());
  });
  EXPECT_CALL(app_, IsFrameTrackEnabled).WillRepeatedly([&](const FunctionInfo& function) -> bool {
    std::optional<size_t> index = get_index_from_function_info(function);
    EXPECT_TRUE(index.has_value());
    return frame_track_enabled.at(index.value());
  });

  auto run_basic_checks = [&](std::vector<int> selected_indices) {
    // Common actions should always be available.
    EXPECT_THAT(view_.GetContextMenu(0, selected_indices),
                testing::IsSupersetOf({"Copy Selection", "Export to CSV"}));

    // Source code and disassembly actions are availble if and only if capture is connected. Hook
    // and unhook actions are unavailable if capture is not connected.
    if (capture_connected) {
      EXPECT_THAT(view_.GetContextMenu(0, selected_indices),
                  testing::IsSupersetOf({"Go to Disassembly", "Go to Source code"}));
    } else {
      EXPECT_THAT(view_.GetContextMenu(0, selected_indices),
                  testing::AllOf(testing::Not(testing::Contains("Go to Disassembly")),
                                 testing::Not(testing::Contains("Go to Source code")),
                                 testing::Not(testing::Contains("Hook")),
                                 testing::Not(testing::Contains("Unhook"))));
    }

    // Jump actions are only available for single selection with non-zero counts.
    if (selected_indices.size() == 1 && kCounts[selected_indices[0]] > 0) {
      EXPECT_THAT(
          view_.GetContextMenu(0, selected_indices),
          testing::IsSupersetOf({"Jump to first", "Jump to last", "Jump to min", "Jump to max"}));
    } else {
      EXPECT_THAT(view_.GetContextMenu(0, selected_indices),
                  testing::AllOf(testing::Not(testing::Contains("Jump to first")),
                                 testing::Not(testing::Contains("Jump to last")),
                                 testing::Not(testing::Contains("Jump to min")),
                                 testing::Not(testing::Contains("Jump to max"))));
    }

    // Add iterators action is only available if some function has non-zero counts.
    int total_counts = 0;
    for (int selected_index : selected_indices) {
      total_counts += kCounts[selected_index];
    }
    if (total_counts > 0) {
      EXPECT_THAT(view_.GetContextMenu(0, selected_indices), testing::Contains("Add iterator(s)"));
    } else {
      EXPECT_THAT(view_.GetContextMenu(0, selected_indices),
                  testing::Not(testing::Contains("Add iterator(s)")));
    }
  };

  const auto can_unhook = [&]() {
    return testing::AllOf(testing::Not(testing::Contains("Hook")), testing::Contains("Unhook"));
  };
  const auto can_hook = [&]() {
    return testing::AllOf(testing::Contains("Hook"), testing::Not(testing::Contains("Unhook")));
  };
  const auto can_hook_and_unhook = [&]() {
    return testing::AllOf(testing::Contains("Hook"), testing::Contains("Unhook"));
  };

  const auto can_disable_frame_tracks = [&]() {
    return testing::AllOf(testing::Not(testing::Contains("Enable frame track(s)")),
                          testing::Contains("Disable frame track(s)"));
  };
  const auto can_enable_frame_tracks = [&]() {
    return testing::AllOf(testing::Contains("Enable frame track(s)"),
                          testing::Not(testing::Contains("Disable frame track(s)")));
  };
  const auto can_enable_and_disable_frame_tracks = [&]() {
    return testing::AllOf(testing::Contains("Enable frame track(s)"),
                          testing::Contains("Disable frame track(s)"));
  };

  capture_connected = false;
  run_basic_checks({0});
  EXPECT_THAT(view_.GetContextMenu(0, {0}), can_enable_frame_tracks());

  run_basic_checks({1});
  EXPECT_THAT(view_.GetContextMenu(0, {1}), can_enable_frame_tracks());

  run_basic_checks({2});
  EXPECT_THAT(view_.GetContextMenu(0, {2}), can_disable_frame_tracks());

  run_basic_checks({0, 1, 2});
  EXPECT_THAT(view_.GetContextMenu(0, {0, 1, 2}), can_enable_and_disable_frame_tracks());

  capture_connected = true;
  run_basic_checks({0});
  EXPECT_THAT(view_.GetContextMenu(0, {0}), can_hook());
  EXPECT_THAT(view_.GetContextMenu(0, {0}), can_enable_frame_tracks());

  run_basic_checks({1});
  EXPECT_THAT(view_.GetContextMenu(0, {1}), can_unhook());
  EXPECT_THAT(view_.GetContextMenu(0, {1}), can_enable_frame_tracks());

  run_basic_checks({2});
  EXPECT_THAT(view_.GetContextMenu(0, {2}), can_unhook());
  EXPECT_THAT(view_.GetContextMenu(0, {2}), can_disable_frame_tracks());

  run_basic_checks({0, 1, 2});
  EXPECT_THAT(view_.GetContextMenu(0, {0, 1, 2}), can_hook_and_unhook());
  EXPECT_THAT(view_.GetContextMenu(0, {0, 1, 2}), can_enable_and_disable_frame_tracks());
}

TEST_F(LiveFunctionsDataViewTest, ContextMenuActionsAreInvoked) {
  bool function_selected = false;
  bool frame_track_enabled = false;
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));
  EXPECT_CALL(app_, IsCaptureConnected).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, IsFunctionSelected).WillRepeatedly(testing::ReturnPointee(&function_selected));
  EXPECT_CALL(app_, IsFrameTrackEnabled)
      .WillRepeatedly(testing::ReturnPointee(&frame_track_enabled));

  AddFunctionsByIndices({0});
  std::vector<std::string> context_menu = view_.GetContextMenu(0, {0});
  ASSERT_FALSE(context_menu.empty());

  // Copy Selection
  {
    const auto copy_selection_index =
        std::find(context_menu.begin(), context_menu.end(), "Copy Selection") -
        context_menu.begin();
    ASSERT_LT(copy_selection_index, context_menu.size());

    std::string clipboard;
    EXPECT_CALL(app_, SetClipboard).Times(1).WillOnce(testing::SaveArg<0>(&clipboard));
    view_.OnContextMenu("Copy Selection", static_cast<int>(copy_selection_index), {0});
    EXPECT_EQ(
        clipboard,
        absl::StrFormat("Hooked\tFunction\tCount\tTotal\tAvg\tMin\tMax\tStd Dev\tModule\tAddress\n"
                        "\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                        kPrettyNames[0], GetExpectedDisplayCount(kCounts[0]),
                        GetExpectedDisplayTime(kTotalTimeNs[0]),
                        GetExpectedDisplayTime(kAvgTimeNs[0]), GetExpectedDisplayTime(kMinNs[0]),
                        GetExpectedDisplayTime(kMaxNs[0]), GetExpectedDisplayTime(kStdDevNs[0]),
                        kModulePaths[0], GetExpectedDisplayAddress(kAddresses[0])));
  }

  // Export to CSV
  {
    const auto export_to_csv_index =
        std::find(context_menu.begin(), context_menu.end(), "Copy Selection") -
        context_menu.begin();
    ASSERT_LT(export_to_csv_index, context_menu.size());

    ErrorMessageOr<orbit_base::TemporaryFile> temporary_file_or_error =
        orbit_base::TemporaryFile::Create();
    ASSERT_THAT(temporary_file_or_error, orbit_base::HasNoError());
    const std::filesystem::path temporary_file_path = temporary_file_or_error.value().file_path();
    temporary_file_or_error.value().CloseAndRemove();

    EXPECT_CALL(app_, GetSaveFile).Times(1).WillOnce(testing::Return(temporary_file_path.string()));
    view_.OnContextMenu("Export to CSV", static_cast<int>(export_to_csv_index), {0});

    ErrorMessageOr<std::string> contents_or_error =
        orbit_base::ReadFileToString(temporary_file_path);
    ASSERT_THAT(contents_or_error, orbit_base::HasNoError());

    EXPECT_EQ(
        contents_or_error.value(),
        absl::StrFormat(
            R"("Hooked","Function","Count","Total","Avg","Min","Max","Std Dev","Module","Address")"
            "\r\n"
            R"("","%s","%s","%s","%s","%s","%s","%s","%s","%s")"
            "\r\n",
            kPrettyNames[0], GetExpectedDisplayCount(kCounts[0]),
            GetExpectedDisplayTime(kTotalTimeNs[0]), GetExpectedDisplayTime(kAvgTimeNs[0]),
            GetExpectedDisplayTime(kMinNs[0]), GetExpectedDisplayTime(kMaxNs[0]),
            GetExpectedDisplayTime(kStdDevNs[0]), kModulePaths[0],
            GetExpectedDisplayAddress(kAddresses[0])));
  }

  // Go to Disassembly
  {
    const auto disassembly_index =
        std::find(context_menu.begin(), context_menu.end(), "Go to Disassembly") -
        context_menu.begin();
    ASSERT_LT(disassembly_index, context_menu.size());

    EXPECT_CALL(app_, Disassemble)
        .Times(1)
        .WillOnce([&](int32_t /*pid*/, const FunctionInfo& function) {
          EXPECT_EQ(function.name(), kNames[0]);
        });
    view_.OnContextMenu("Go to Disassembly", static_cast<int>(disassembly_index), {0});
  }

  // Go to Source code
  {
    const auto source_code_index =
        std::find(context_menu.begin(), context_menu.end(), "Go to Source code") -
        context_menu.begin();
    ASSERT_LT(source_code_index, context_menu.size());

    EXPECT_CALL(app_, ShowSourceCode).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kNames[0]);
    });
    view_.OnContextMenu("Go to Source code", static_cast<int>(source_code_index), {0});
  }

  // Jump to first
  {
    const auto jump_to_first_index =
        std::find(context_menu.begin(), context_menu.end(), "Jump to first") - context_menu.begin();
    ASSERT_LT(jump_to_first_index, context_menu.size());

    EXPECT_CALL(app_, JumpToTimerAndZoom)
        .Times(1)
        .WillOnce([](uint64_t /*function_id*/, JumpToTimerMode selection_mode) {
          EXPECT_EQ(selection_mode, JumpToTimerMode::kFirst);
        });
    view_.OnContextMenu("Jump to first", static_cast<int>(jump_to_first_index), {0});
  }

  // Jump to last
  {
    const auto jump_to_last_index =
        std::find(context_menu.begin(), context_menu.end(), "Jump to last") - context_menu.begin();
    ASSERT_LT(jump_to_last_index, context_menu.size());

    EXPECT_CALL(app_, JumpToTimerAndZoom)
        .Times(1)
        .WillOnce([](uint64_t /*function_id*/, JumpToTimerMode selection_mode) {
          EXPECT_EQ(selection_mode, JumpToTimerMode::kLast);
        });
    view_.OnContextMenu("Jump to last", static_cast<int>(jump_to_last_index), {0});
  }

  // Jump to min
  {
    const auto jump_to_min_index =
        std::find(context_menu.begin(), context_menu.end(), "Jump to min") - context_menu.begin();
    ASSERT_LT(jump_to_min_index, context_menu.size());

    EXPECT_CALL(app_, JumpToTimerAndZoom)
        .Times(1)
        .WillOnce([](uint64_t /*function_id*/, JumpToTimerMode selection_mode) {
          EXPECT_EQ(selection_mode, JumpToTimerMode::kMin);
        });
    view_.OnContextMenu("Jump to min", static_cast<int>(jump_to_min_index), {0});
  }

  // Jump to max
  {
    const auto jump_to_max_index =
        std::find(context_menu.begin(), context_menu.end(), "Jump to max") - context_menu.begin();
    ASSERT_LT(jump_to_max_index, context_menu.size());

    EXPECT_CALL(app_, JumpToTimerAndZoom)
        .Times(1)
        .WillOnce([](uint64_t /*function_id*/, JumpToTimerMode selection_mode) {
          EXPECT_EQ(selection_mode, JumpToTimerMode::kMax);
        });
    view_.OnContextMenu("Jump to max", static_cast<int>(jump_to_max_index), {0});
  }

  // Add iterator(s)
  {
    const auto add_iterators_index =
        std::find(context_menu.begin(), context_menu.end(), "Add iterator(s)") -
        context_menu.begin();
    ASSERT_LT(add_iterators_index, context_menu.size());

    EXPECT_CALL(live_functions_, AddIterator)
        .Times(1)
        .WillOnce([&](uint64_t instrumented_function_id, const FunctionInfo* function) {
          EXPECT_EQ(instrumented_function_id, kFunctionIds[0]);
          EXPECT_EQ(function->name(), kNames[0]);
        });
    view_.OnContextMenu("Add iterator(s)", static_cast<int>(add_iterators_index), {0});
  }

  // Hook
  {
    const auto hook_index =
        std::find(context_menu.begin(), context_menu.end(), "Hook") - context_menu.begin();
    ASSERT_LT(hook_index, context_menu.size());

    EXPECT_CALL(app_, SelectFunction).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kNames[0]);
    });
    view_.OnContextMenu("Hook", static_cast<int>(hook_index), {0});
  }

  // Enable frame track(s)
  {
    const auto enable_frame_track_index =
        std::find(context_menu.begin(), context_menu.end(), "Enable frame track(s)") -
        context_menu.begin();
    ASSERT_LT(enable_frame_track_index, context_menu.size());

    EXPECT_CALL(app_, SelectFunction).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kNames[0]);
    });
    EXPECT_CALL(app_, EnableFrameTrack).Times(1);
    EXPECT_CALL(app_, AddFrameTrack(testing::An<uint64_t>()))
        .Times(1)
        .WillOnce([&](uint64_t function_id) { EXPECT_EQ(function_id, kFunctionIds[0]); });
    view_.OnContextMenu("Enable frame track(s)", static_cast<int>(enable_frame_track_index), {0});
  }

  function_selected = true;
  frame_track_enabled = true;
  capture_data_->EnableFrameTrack(kFunctionIds[0]);
  context_menu = view_.GetContextMenu(0, {0});
  ASSERT_FALSE(context_menu.empty());

  // Unhook
  {
    const auto unhook_index =
        std::find(context_menu.begin(), context_menu.end(), "Unhook") - context_menu.begin();
    ASSERT_LT(unhook_index, context_menu.size());

    EXPECT_CALL(app_, DeselectFunction).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kNames[0]);
    });
    EXPECT_CALL(app_, DisableFrameTrack).Times(1);
    EXPECT_CALL(app_, RemoveFrameTrack(testing::An<const FunctionInfo&>()))
        .Times(1)
        .WillOnce([&](const FunctionInfo& function) { EXPECT_EQ(function.name(), kNames[0]); });
    view_.OnContextMenu("Unhook", static_cast<int>(unhook_index), {0});
  }

  // Disable frame track(s)
  {
    const auto disable_frame_track_index =
        std::find(context_menu.begin(), context_menu.end(), "Disable frame track(s)") -
        context_menu.begin();
    ASSERT_LT(disable_frame_track_index, context_menu.size());

    EXPECT_CALL(app_, DisableFrameTrack).Times(1).WillOnce([&](const FunctionInfo& function) {
      EXPECT_EQ(function.name(), kNames[0]);
    });
    EXPECT_CALL(app_, RemoveFrameTrack(testing::An<uint64_t>()))
        .Times(1)
        .WillOnce([&](uint64_t function_id) { EXPECT_EQ(function_id, kFunctionIds[0]); });
    view_.OnContextMenu("Disable frame track(s)", static_cast<int>(disable_frame_track_index), {0});
  }
}

TEST_F(LiveFunctionsDataViewTest, FilteringShowsRightResults) {
  AddFunctionsByIndices({0, 1, 2});
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));

  // Filtering by function display name with single token
  {
    EXPECT_CALL(app_, SetVisibleFunctionIds)
        .Times(1)
        .WillOnce([&](absl::flat_hash_set<uint64_t> visible_function_ids) {
          EXPECT_THAT(visible_function_ids,
                      testing::UnorderedElementsAre(kFunctionIds[1], kFunctionIds[2]));
        });
    view_.OnFilter("int");
    EXPECT_EQ(view_.GetNumElements(), 2);
    EXPECT_THAT((std::array{view_.GetValue(0, kColumnName), view_.GetValue(1, kColumnName)}),
                testing::UnorderedElementsAre(kPrettyNames[1], kPrettyNames[2]));
  }

  // Filtering by function display name with multiple tokens separated by " "
  {
    EXPECT_CALL(app_, SetVisibleFunctionIds)
        .Times(1)
        .WillOnce([&](absl::flat_hash_set<uint64_t> visible_function_ids) {
          EXPECT_THAT(visible_function_ids, testing::UnorderedElementsAre(kFunctionIds[1]));
        });

    view_.OnFilter("int main");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_EQ(view_.GetValue(0, kColumnName), kPrettyNames[1]);
  }

  // No matching result
  {
    EXPECT_CALL(app_, SetVisibleFunctionIds)
        .Times(1)
        .WillOnce([](absl::flat_hash_set<uint64_t> visible_function_ids) {
          EXPECT_TRUE(visible_function_ids.empty());
        });
    view_.OnFilter("int module");
    EXPECT_EQ(view_.GetNumElements(), 0);
  }
}

TEST_F(LiveFunctionsDataViewTest, UpdateHighlightedFunctionsOnSelect) {
  AddFunctionsByIndices({0, 1, 2});

  EXPECT_CALL(app_, DeselectTimer).Times(3);
  EXPECT_CALL(app_, GetHighlightedFunctionId).Times(3);
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));

  // Single selection will hightlight the selected function
  {
    EXPECT_CALL(app_, SetHighlightedFunctionId)
        .Times(1)
        .WillOnce([&](uint64_t highlighted_function_id) {
          EXPECT_EQ(highlighted_function_id, kFunctionIds[2]);
        });

    view_.OnSelect({2});
  }

  // Multiple selection will hightlight the first selected function
  {
    EXPECT_CALL(app_, SetHighlightedFunctionId)
        .Times(1)
        .WillOnce([&](uint64_t highlighted_function_id) {
          EXPECT_EQ(highlighted_function_id, kFunctionIds[1]);
        });

    view_.OnSelect({1, 2});
  }

  // Empty selection will clear the function highlighting
  {
    EXPECT_CALL(app_, SetHighlightedFunctionId)
        .Times(1)
        .WillOnce([&](uint64_t highlighted_function_id) {
          EXPECT_EQ(highlighted_function_id, orbit_grpc_protos::kInvalidFunctionId);
        });

    view_.OnSelect({});
  }
}

TEST_F(LiveFunctionsDataViewTest, ColumnSortingShowsRightResults) {
  AddFunctionsByIndices({0, 1, 2});
  EXPECT_CALL(app_, HasCaptureData).WillRepeatedly(testing::Return(true));
  EXPECT_CALL(app_, GetCaptureData).WillRepeatedly(testing::ReturnRef(*capture_data_));

  using ViewRowEntry = std::array<std::string, kNumColumns>;
  std::vector<ViewRowEntry> view_entries;
  absl::flat_hash_map<std::string, uint64_t> string_to_raw_value;
  for (const auto [function_id, function] : functions_) {
    const FunctionStats& stats = capture_data_->GetFunctionStatsOrDefault(function_id);

    ViewRowEntry entry;
    entry[kColumnName] = function.pretty_name();
    entry[kColumnModule] = function.module_path();
    entry[kColumnAddress] = GetExpectedDisplayAddress(function.address());
    entry[kColumnCount] = GetExpectedDisplayCount(stats.count());
    string_to_raw_value.insert_or_assign(entry[kColumnCount], stats.count());
    entry[kColumnTimeTotal] = GetExpectedDisplayTime(stats.total_time_ns());
    string_to_raw_value.insert_or_assign(entry[kColumnTimeTotal], stats.total_time_ns());
    entry[kColumnTimeAvg] = GetExpectedDisplayTime(stats.average_time_ns());
    string_to_raw_value.insert_or_assign(entry[kColumnTimeAvg], stats.average_time_ns());
    entry[kColumnTimeMin] = GetExpectedDisplayTime(stats.min_ns());
    string_to_raw_value.insert_or_assign(entry[kColumnTimeMin], stats.min_ns());
    entry[kColumnTimeMax] = GetExpectedDisplayTime(stats.max_ns());
    string_to_raw_value.insert_or_assign(entry[kColumnTimeMax], stats.max_ns());
    entry[kColumnStdDev] = GetExpectedDisplayTime(stats.std_dev_ns());
    string_to_raw_value.insert_or_assign(entry[kColumnStdDev], stats.std_dev_ns());

    view_entries.push_back(entry);
  }

  auto sort_and_verify = [&](int column, orbit_data_views::DataView::SortingOrder order) {
    view_.OnSort(column, order);

    switch (column) {
      case kColumnName:
      case kColumnModule:
      case kColumnAddress:
        // Columns of name, module path and address are sort by display values (i.e., string).
        std::sort(view_entries.begin(), view_entries.end(),
                  [column, order](const ViewRowEntry& lhs, const ViewRowEntry& rhs) {
                    switch (order) {
                      case orbit_data_views::DataView::SortingOrder::kAscending:
                        return lhs[column] < rhs[column];
                      case orbit_data_views::DataView::SortingOrder::kDescending:
                        return lhs[column] > rhs[column];
                      default:
                        UNREACHABLE();
                    }
                  });
        break;
      case kColumnCount:
      case kColumnTimeTotal:
      case kColumnTimeAvg:
      case kColumnTimeMin:
      case kColumnTimeMax:
      case kColumnStdDev:
        // Columns of count and time statistics are sorted by raw values (i.e., uint64_t).
        std::sort(
            view_entries.begin(), view_entries.end(),
            [column, order, string_to_raw_value](const ViewRowEntry& lhs, const ViewRowEntry& rhs) {
              switch (order) {
                case orbit_data_views::DataView::SortingOrder::kAscending:
                  return string_to_raw_value.at(lhs[column]) < string_to_raw_value.at(rhs[column]);
                case orbit_data_views::DataView::SortingOrder::kDescending:
                  return string_to_raw_value.at(lhs[column]) > string_to_raw_value.at(rhs[column]);
                default:
                  UNREACHABLE();
              }
            });
        break;
      default:
        UNREACHABLE();
    }

    for (size_t index = 0; index < view_entries.size(); ++index) {
      for (int column = kColumnName; column < kNumColumns; ++column) {
        EXPECT_EQ(view_.GetValue(index, column), view_entries[index][column]);
      }
    }
  };

  for (int column = kColumnName; column < kNumColumns; ++column) {
    // Sort by ascending
    { sort_and_verify(column, orbit_data_views::DataView::SortingOrder::kAscending); }

    // Sort by descending
    { sort_and_verify(column, orbit_data_views::DataView::SortingOrder::kDescending); }
  }
}
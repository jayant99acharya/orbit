// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/strings/str_format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ClientData/ProcessData.h"
#include "DataViews/DataView.h"
#include "DataViews/ModulesDataView.h"
#include "DisplayFormats/DisplayFormats.h"
#include "MockAppInterface.h"
#include "OrbitBase/File.h"
#include "OrbitBase/ReadFileToString.h"
#include "OrbitBase/TemporaryFile.h"
#include "OrbitBase/TestUtils.h"
#include "module.pb.h"

using orbit_client_data::ModuleData;
using orbit_client_data::ModuleInMemory;
using orbit_grpc_protos::ModuleInfo;

namespace {

constexpr size_t kNumModules = 3;
constexpr std::array<uint64_t, kNumModules> kStartAddresses{0x1000, 0x2000, 0x3000};
constexpr std::array<uint64_t, kNumModules> kEndAddresses{0x1100, 0x2100, 0x3100};
constexpr std::array<uint64_t, kNumModules> kSizes{300, 100, 200};
const std::array<std::string, kNumModules> kNames{"module_abc", "module_abc", "module_xyz"};
const std::array<std::string, kNumModules> kFilePaths{
    "/usr/subpath/to/module_abc", "/local/subpath/to/module_abc", "/usr/subpath/to/module_xyz"};
const std::array<std::string, kNumModules> kBuildIds{"build_id_0", "build_id_1", "build_id_2"};

// ModulesDataView also has column index constants defined, but they are declared as private.
constexpr int kColumnName = 0;
constexpr int kColumnPath = 1;
constexpr int kColumnAddressRange = 2;
constexpr int kColumnFileSize = 3;
constexpr int kColumnLoaded = 4;
constexpr int kNumColumns = 5;

std::string GetExpectedDisplayAddressRangeByIndex(size_t index) {
  return absl::StrFormat("[%016x - %016x]", kStartAddresses[index], kEndAddresses[index]);
}

std::string GetExpectedDisplayFileSizeByIndex(size_t index) {
  return orbit_display_formats::GetDisplaySize(kSizes[index]);
}

class ModulesDataViewTest : public testing::Test {
 public:
  explicit ModulesDataViewTest() : view_{&app_} {
    for (size_t i = 0; i < kNumModules; i++) {
      ModuleInMemory module_in_memory{kStartAddresses[i], kEndAddresses[i], kFilePaths[i],
                                      kBuildIds[i]};
      modules_in_memory_.push_back(module_in_memory);

      ModuleInfo module_info{};
      module_info.set_name(kNames[i]);
      module_info.set_file_path(kFilePaths[i]);
      module_info.set_build_id(kBuildIds[i]);
      module_info.set_file_size(kSizes[i]);
      EXPECT_TRUE(module_manager_.AddOrUpdateModules({module_info}).empty());
    }
  }

  void AddModulesByIndices(const std::vector<size_t>& indices) {
    std::set index_set(indices.begin(), indices.end());
    for (size_t index : index_set) {
      CHECK(index < kNumModules);
      view_.AddModule(
          modules_in_memory_[index].start(),
          module_manager_.GetMutableModuleByPathAndBuildId(modules_in_memory_[index].file_path(),
                                                           modules_in_memory_[index].build_id()),
          modules_in_memory_[index]);
    }
  }

 protected:
  orbit_data_views::MockAppInterface app_;
  orbit_data_views::ModulesDataView view_;
  orbit_client_data::ModuleManager module_manager_;
  std::vector<ModuleInMemory> modules_in_memory_;
};

}  // namespace

TEST_F(ModulesDataViewTest, ColumnHeadersNotEmpty) {
  EXPECT_GE(view_.GetColumns().size(), 1);
  for (const auto& column : view_.GetColumns()) {
    EXPECT_FALSE(column.header.empty());
  }
}

TEST_F(ModulesDataViewTest, HasValidDefaultSortingColumn) {
  EXPECT_GE(view_.GetDefaultSortingColumn(), kColumnFileSize);
  EXPECT_LT(view_.GetDefaultSortingColumn(), view_.GetColumns().size());
}

TEST_F(ModulesDataViewTest, ColumnValuesAreCorrect) {
  AddModulesByIndices({0});

  EXPECT_EQ(view_.GetValue(0, kColumnName), kNames[0]);
  EXPECT_EQ(view_.GetValue(0, kColumnPath), kFilePaths[0]);
  EXPECT_EQ(view_.GetValue(0, kColumnAddressRange), GetExpectedDisplayAddressRangeByIndex(0));
  EXPECT_EQ(view_.GetValue(0, kColumnFileSize), GetExpectedDisplayFileSizeByIndex(0));
  EXPECT_EQ(view_.GetValue(0, kColumnLoaded), "");
}

TEST_F(ModulesDataViewTest, ContextMenuEntriesArePresent) {
  AddModulesByIndices({0});

  EXPECT_THAT(view_.GetContextMenu(0, {0}),
              testing::UnorderedElementsAre("Load Symbols", "Copy Selection", "Export to CSV"));
}

TEST_F(ModulesDataViewTest, ContextMenuActionsAreInvoked) {
  AddModulesByIndices({0});
  std::vector<std::string> context_menu = view_.GetContextMenu(0, {0});
  ASSERT_FALSE(context_menu.empty());

  // Load Symbols
  {
    const auto load_symbols_index =
        std::find(context_menu.begin(), context_menu.end(), "Load Symbols") - context_menu.begin();
    ASSERT_LT(load_symbols_index, context_menu.size());

    EXPECT_CALL(app_, RetrieveModulesAndLoadSymbols)
        .Times(1)
        .WillOnce(testing::Return(orbit_base::Future<void>{}));
    view_.OnContextMenu("Load Symbols", static_cast<int>(load_symbols_index), {0});
  }

  // Copy Selection
  {
    const auto copy_selection_index =
        std::find(context_menu.begin(), context_menu.end(), "Copy Selection") -
        context_menu.begin();
    ASSERT_LT(copy_selection_index, context_menu.size());

    std::string clipboard;
    EXPECT_CALL(app_, SetClipboard).Times(1).WillOnce(testing::SaveArg<0>(&clipboard));
    view_.OnContextMenu("Copy Selection", static_cast<int>(copy_selection_index), {0});
    EXPECT_EQ(clipboard,
              absl::StrFormat("Name\tPath\tAddress Range\tFile Size\tLoaded\n"
                              "%s\t%s\t%s\t%s\t\n",
                              kNames[0], kFilePaths[0], GetExpectedDisplayAddressRangeByIndex(0),
                              GetExpectedDisplayFileSizeByIndex(0)));
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

    EXPECT_EQ(contents_or_error.value(),
              absl::StrFormat(R"("Name","Path","Address Range","File Size","Loaded")"
                              "\r\n"
                              R"("%s","%s","%s","%s","")"
                              "\r\n",
                              kNames[0], kFilePaths[0], GetExpectedDisplayAddressRangeByIndex(0),
                              GetExpectedDisplayFileSizeByIndex(0)));
  }
}

TEST_F(ModulesDataViewTest, LoadModuleOnDoubleClick) {
  EXPECT_CALL(app_, RetrieveModulesAndLoadSymbols)
      .Times(1)
      .WillOnce(testing::Return(orbit_base::Future<void>{}));

  AddModulesByIndices({0});
  view_.OnDoubleClicked(0);
}

TEST_F(ModulesDataViewTest, FilteringShowsRightResults) {
  AddModulesByIndices({0, 1, 2});

  // Filtering by path with single token
  {
    view_.OnFilter("abc");
    EXPECT_EQ(view_.GetNumElements(), 2);
    EXPECT_THAT((std::array{view_.GetValue(0, kColumnPath), view_.GetValue(1, kColumnPath)}),
                testing::UnorderedElementsAre(kFilePaths[0], kFilePaths[1]));
  }

  // Filtering by path with multiple tokens separated by " ".
  {
    view_.OnFilter("abc usr");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_EQ(view_.GetValue(0, kColumnPath), kFilePaths[0]);
  }

  // Fileter by address range
  {
    view_.OnFilter("1100");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_EQ(view_.GetValue(0, kColumnAddressRange), GetExpectedDisplayAddressRangeByIndex(0));
  }

  // Filter by path and address range
  {
    view_.OnFilter("abc 1100");
    EXPECT_EQ(view_.GetNumElements(), 1);
    EXPECT_EQ(view_.GetValue(0, kColumnPath), kFilePaths[0]);
    EXPECT_EQ(view_.GetValue(0, kColumnAddressRange), GetExpectedDisplayAddressRangeByIndex(0));
  }

  // No matching result
  {
    view_.OnFilter("abcdefg");
    EXPECT_EQ(view_.GetNumElements(), 0);
  }
}

TEST_F(ModulesDataViewTest, ColumnSortingShowsRightResults) {
  AddModulesByIndices({0, 1, 2});

  using ViewRowEntry = std::array<std::string, kNumColumns>;
  std::vector<ViewRowEntry> view_entries;
  for (const ModuleInMemory& module_in_memory : modules_in_memory_) {
    const ModuleData* module = module_manager_.GetMutableModuleByPathAndBuildId(
        module_in_memory.file_path(), module_in_memory.build_id());

    ViewRowEntry entry;
    entry[kColumnName] = module->name();
    entry[kColumnPath] = module->file_path();
    entry[kColumnFileSize] = orbit_display_formats::GetDisplaySize(module->file_size());
    entry[kColumnAddressRange] =
        absl::StrFormat("[%016x - %016x]", module_in_memory.start(), module_in_memory.end());

    view_entries.push_back(entry);
  }

  auto sort_and_verify = [&](int column_index, orbit_data_views::DataView::SortingOrder order) {
    view_.OnSort(column_index, order);

    std::sort(view_entries.begin(), view_entries.end(),
              [column_index, order](const auto& lhs, const auto& rhs) {
                switch (order) {
                  case orbit_data_views::DataView::SortingOrder::kAscending:
                    return lhs[column_index] < rhs[column_index];
                  case orbit_data_views::DataView::SortingOrder::kDescending:
                    return lhs[column_index] > rhs[column_index];
                  default:
                    UNREACHABLE();
                }
              });

    for (size_t index = 0; index < view_entries.size(); ++index) {
      EXPECT_EQ(view_.GetValue(index, kColumnName), view_entries[index][kColumnName]);
      EXPECT_EQ(view_.GetValue(index, kColumnPath), view_entries[index][kColumnPath]);
      EXPECT_EQ(view_.GetValue(index, kColumnFileSize), view_entries[index][kColumnFileSize]);
      EXPECT_EQ(view_.GetValue(index, kColumnAddressRange),
                view_entries[index][kColumnAddressRange]);
    }
  };

  // Sort by name ascending
  { sort_and_verify(kColumnName, orbit_data_views::DataView::SortingOrder::kAscending); }

  // Sort by name descending
  { sort_and_verify(kColumnName, orbit_data_views::DataView::SortingOrder::kDescending); }

  // Sort by path ascending
  { sort_and_verify(kColumnPath, orbit_data_views::DataView::SortingOrder::kAscending); }

  // Sort by path descending
  { sort_and_verify(kColumnPath, orbit_data_views::DataView::SortingOrder::kDescending); }

  // Sort by file size ascending
  { sort_and_verify(kColumnFileSize, orbit_data_views::DataView::SortingOrder::kAscending); }

  // Sort by file size descending
  { sort_and_verify(kColumnFileSize, orbit_data_views::DataView::SortingOrder::kDescending); }

  // Sort by address range ascending
  { sort_and_verify(kColumnAddressRange, orbit_data_views::DataView::SortingOrder::kAscending); }

  // Sort by address range descending
  { sort_and_verify(kColumnAddressRange, orbit_data_views::DataView::SortingOrder::kDescending); }
}

// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "DataViews/ModulesDataView.h"

#include <absl/flags/declare.h>
#include <absl/flags/flag.h>
#include <absl/strings/ascii.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <functional>

#include "ClientData/ProcessData.h"
#include "ClientFlags/ClientFlags.h"
#include "CompareAscendingOrDescending.h"
#include "DataViews/DataViewType.h"
#include "DisplayFormats/DisplayFormats.h"
#include "OrbitBase/Append.h"
#include "OrbitBase/Logging.h"

using orbit_client_data::ModuleData;
using orbit_client_data::ModuleInMemory;
using orbit_client_data::ProcessData;

namespace orbit_data_views {

ModulesDataView::ModulesDataView(AppInterface* app) : DataView(DataViewType::kModules, app) {}

const std::vector<DataView::Column>& ModulesDataView::GetColumns() {
  static const std::vector<Column> columns = [] {
    std::vector<Column> columns;
    columns.resize(kNumColumns);
    columns[kColumnName] = {"Name", .2f, SortingOrder::kAscending};
    columns[kColumnPath] = {"Path", .5f, SortingOrder::kAscending};
    columns[kColumnAddressRange] = {"Address Range", .15f, SortingOrder::kAscending};
    columns[kColumnFileSize] = {"File Size", .0f, SortingOrder::kDescending};
    columns[kColumnLoaded] = {"Loaded", .0f, SortingOrder::kDescending};
    return columns;
  }();
  return columns;
}

std::string ModulesDataView::GetValue(int row, int col) {
  uint64_t start_address = indices_[row];
  const ModuleData* module = start_address_to_module_.at(start_address);
  const ModuleInMemory& memory_space = start_address_to_module_in_memory_.at(start_address);

  switch (col) {
    case kColumnName:
      return module->name();
    case kColumnPath:
      return module->file_path();
    case kColumnAddressRange:
      return memory_space.FormattedAddressRange();
    case kColumnFileSize:
      return orbit_display_formats::GetDisplaySize(module->file_size());
    case kColumnLoaded:
      return module->is_loaded() ? "*" : "";
    default:
      return "";
  }
}

#define ORBIT_PROC_SORT(Member)                                                             \
  [&](uint64_t a, uint64_t b) {                                                             \
    return CompareAscendingOrDescending(start_address_to_module_.at(a)->Member,             \
                                        start_address_to_module_.at(b)->Member, ascending); \
  }

#define ORBIT_MODULE_SPACE_SORT(Member)                                                  \
  [&](uint64_t a, uint64_t b) {                                                          \
    return CompareAscendingOrDescending(start_address_to_module_in_memory_.at(a).Member, \
                                        start_address_to_module_in_memory_.at(b).Member, \
                                        ascending);                                      \
  }

void ModulesDataView::DoSort() {
  bool ascending = sorting_orders_[sorting_column_] == SortingOrder::kAscending;
  std::function<bool(uint64_t, uint64_t)> sorter = nullptr;

  switch (sorting_column_) {
    case kColumnName:
      sorter = ORBIT_PROC_SORT(name());
      break;
    case kColumnPath:
      sorter = ORBIT_PROC_SORT(file_path());
      break;
    case kColumnAddressRange:
      sorter = ORBIT_MODULE_SPACE_SORT(start());
      break;
    case kColumnFileSize:
      sorter = ORBIT_PROC_SORT(file_size());
      break;
    case kColumnLoaded:
      sorter = ORBIT_PROC_SORT(is_loaded());
      break;
    default:
      break;
  }

  if (sorter) {
    std::stable_sort(indices_.begin(), indices_.end(), sorter);
  }
}

const std::string ModulesDataView::kMenuActionLoadSymbols = "Load Symbols";
const std::string ModulesDataView::kMenuActionVerifyFramePointers = "Verify Frame Pointers";

std::vector<std::string> ModulesDataView::GetContextMenu(int clicked_index,
                                                         const std::vector<int>& selected_indices) {
  bool enable_load = false;
  bool enable_verify = false;
  for (int index : selected_indices) {
    const ModuleData* module = GetModule(index);
    if (!module->is_loaded()) {
      enable_load = true;
    }

    if (module->is_loaded()) {
      enable_verify = true;
    }
  }

  std::vector<std::string> menu;
  if (enable_load) {
    menu.emplace_back(kMenuActionLoadSymbols);
  }
  if (enable_verify && absl::GetFlag(FLAGS_enable_frame_pointer_validator)) {
    menu.emplace_back(kMenuActionVerifyFramePointers);
  }
  orbit_base::Append(menu, DataView::GetContextMenu(clicked_index, selected_indices));
  return menu;
}

void ModulesDataView::OnContextMenu(const std::string& action, int menu_index,
                                    const std::vector<int>& item_indices) {
  if (action == kMenuActionLoadSymbols) {
    std::vector<ModuleData*> modules_to_load;
    for (int index : item_indices) {
      ModuleData* module_data = GetModule(index);
      if (!module_data->is_loaded()) {
        modules_to_load.push_back(module_data);
      }
    }
    app_->RetrieveModulesAndLoadSymbols(modules_to_load);

  } else if (action == kMenuActionVerifyFramePointers) {
    std::vector<const ModuleData*> modules_to_validate;
    modules_to_validate.reserve(item_indices.size());
    for (int index : item_indices) {
      const ModuleData* module = GetModule(index);
      modules_to_validate.push_back(module);
    }

    if (!modules_to_validate.empty()) {
      app_->OnValidateFramePointers(modules_to_validate);
    }
  } else {
    DataView::OnContextMenu(action, menu_index, item_indices);
  }
}

void ModulesDataView::OnDoubleClicked(int index) {
  ModuleData* module_data = GetModule(index);
  if (!module_data->is_loaded()) {
    std::vector<ModuleData*> modules_to_load = {module_data};
    app_->RetrieveModulesAndLoadSymbols(modules_to_load);
  }
}

void ModulesDataView::DoFilter() {
  std::vector<uint64_t> indices;
  std::vector<std::string> tokens = absl::StrSplit(absl::AsciiStrToLower(filter_), ' ');

  for (const auto& [start_address, memory_space] : start_address_to_module_in_memory_) {
    std::string module_string = absl::StrFormat("%s %s", memory_space.FormattedAddressRange(),
                                                absl::AsciiStrToLower(memory_space.file_path()));

    bool match = true;

    for (std::string& filter_token : tokens) {
      if (!absl::StrContains(module_string, filter_token)) {
        match = false;
        break;
      }
    }

    if (match) {
      indices.push_back(start_address);
    }
  }

  indices_ = std::move(indices);
}

void ModulesDataView::AddModule(uint64_t start_address, ModuleData* module,
                                ModuleInMemory module_in_memory) {
  start_address_to_module_.insert_or_assign(start_address, std::move(module));
  start_address_to_module_in_memory_.insert_or_assign(start_address, std::move(module_in_memory));
  indices_.push_back(start_address);
}

void ModulesDataView::UpdateModules(const ProcessData* process) {
  start_address_to_module_.clear();
  start_address_to_module_in_memory_.clear();
  auto memory_map = process->GetMemoryMapCopy();
  indices_.clear();
  for (const auto& [start_address, module_in_memory] : memory_map) {
    ModuleData* module = app_->GetMutableModuleByPathAndBuildId(module_in_memory.file_path(),
                                                                module_in_memory.build_id());
    AddModule(start_address, std::move(module), module_in_memory);
  }

  OnDataChanged();
}

void ModulesDataView::OnRefreshButtonClicked() {
  const ProcessData* process = app_->GetTargetProcess();
  if (process == nullptr) {
    LOG("Unable to refresh module list, no process selected");
    return;
  }
  app_->UpdateProcessAndModuleList();
}

bool ModulesDataView::GetDisplayColor(int row, int /*column*/, unsigned char& red,
                                      unsigned char& green, unsigned char& blue) {
  const ModuleData* module = GetModule(row);
  if (module->is_loaded()) {
    red = 42;
    green = 218;
    blue = 130;
  } else {
    red = 42;
    green = 130;
    blue = 218;
  }
  return true;
}

}  // namespace orbit_data_views

// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_SERVICES_PROCESS_CLIENT_H_
#define CLIENT_SERVICES_PROCESS_CLIENT_H_

#include <grpcpp/grpcpp.h>
#include <stdint.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "OrbitBase/Result.h"
#include "module.pb.h"
#include "process.pb.h"
#include "services.grpc.pb.h"

namespace orbit_client_services {

// This class handles the client calls related to process service.
class ProcessClient {
 public:
  explicit ProcessClient(const std::shared_ptr<grpc::Channel>& channel)
      : process_service_(orbit_grpc_protos::ProcessService::NewStub(channel)) {}

  [[nodiscard]] ErrorMessageOr<std::vector<orbit_grpc_protos::ProcessInfo>> GetProcessList();

  [[nodiscard]] ErrorMessageOr<std::vector<orbit_grpc_protos::ModuleInfo>> LoadModuleList(
      uint32_t pid);

  [[nodiscard]] ErrorMessageOr<std::string> FindDebugInfoFile(const std::string& module_path);

  [[nodiscard]] ErrorMessageOr<std::string> LoadProcessMemory(uint32_t pid, uint64_t address,
                                                              uint64_t size);

 private:
  std::unique_ptr<orbit_grpc_protos::ProcessService::Stub> process_service_;
};

}  // namespace orbit_client_services

#endif  // CLIENT_SERVICES_PROCESS_CLIENT_H_

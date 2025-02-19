// Copyright (c) 2021 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTROSPECTION_INTROSPECTION_H_
#define INTROSPECTION_INTROSPECTION_H_

#include <stdint.h>

#include <functional>
#include <memory>

#include "ApiInterface/Orbit.h"
#include "ApiUtils/Event.h"
#include "OrbitBase/ThreadPool.h"
#include "OrbitBase/ThreadUtils.h"

#define ORBIT_SCOPE_FUNCTION ORBIT_SCOPE(__FUNCTION__)

namespace orbit_introspection {

using TracingEventCallback = std::function<void(const orbit_api::ApiEventVariant& api_event)>;

class TracingListener {
 public:
  explicit TracingListener(TracingEventCallback callback);
  ~TracingListener();

  static void DeferApiEventProcessing(const orbit_api::ApiEventVariant& api_event);
  [[nodiscard]] inline static bool IsActive() { return active_; }
  [[nodiscard]] inline static bool IsShutdownInitiated() { return shutdown_initiated_; }

 private:
  TracingEventCallback user_callback_ = nullptr;
  std::shared_ptr<orbit_base::ThreadPool> thread_pool_ = {};
  inline static bool active_ = false;
  inline static bool shutdown_initiated_ = true;
};

}  // namespace orbit_introspection

#endif  // INTROSPECTION_INTROSPECTION_H_

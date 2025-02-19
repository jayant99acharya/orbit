// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "AccessibleTimeGraph.h"

#include <vector>

#include "AccessibleInterfaceProvider.h"
#include "AccessibleTrack.h"
#include "TimeGraph.h"
#include "Track.h"
#include "TrackManager.h"
#include "Viewport.h"

using orbit_accessibility::AccessibilityRect;
using orbit_accessibility::AccessibilityState;
using orbit_accessibility::AccessibleInterface;

namespace orbit_gl {

AccessibilityRect TimeGraphAccessibility::AccessibleRect() const {
  const Viewport* viewport = time_graph_->GetViewport();

  return AccessibilityRect(0, 0, viewport->WorldToScreenWidth(time_graph_->GetWidth()),
                           viewport->GetScreenHeight());
}

AccessibilityState TimeGraphAccessibility::AccessibleState() const {
  return AccessibilityState::Focusable;
}

int TimeGraphAccessibility::AccessibleChildCount() const {
  return time_graph_->GetTrackManager()->GetVisibleTracks().size();
}

const AccessibleInterface* TimeGraphAccessibility::AccessibleChild(int index) const {
  return time_graph_->GetTrackManager()
      ->GetVisibleTracks()[index]
      ->GetOrCreateAccessibleInterface();
}

const AccessibleInterface* TimeGraphAccessibility::AccessibleParent() const {
  // Special handling given than GlCanvas is not a CaptureViewElement.
  return time_graph_->GetAccessibleParent()->GetOrCreateAccessibleInterface();
}

}  // namespace orbit_gl
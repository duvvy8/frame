// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/resource_coordinator/frame_tab_state.h"

namespace resource_coordinator {

FrameTabState::FrameTabState(content::WebContents* contents)
    : content::WebContentsUserData<FrameTabState>(*contents) {}

FrameTabState* FrameTabState::GetOrCreate(content::WebContents* contents) {
  CreateForWebContents(contents);
  return FromWebContents(contents);
}

void FrameTabState::Restore(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  auto found = extra_data.find(kNeverSleepKey);
  if (found != extra_data.end()) {
    GetOrCreate(contents)->set_never_sleep(found->second == "1");
  }
}

void FrameTabState::Populate(
    content::WebContents* contents,
    std::map<std::string, std::string>* extra_data) {
  if (auto* state = FromWebContents(contents)) {
    (*extra_data)[kNeverSleepKey] = state->never_sleep() ? "1" : "0";
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(FrameTabState);

}  // namespace resource_coordinator

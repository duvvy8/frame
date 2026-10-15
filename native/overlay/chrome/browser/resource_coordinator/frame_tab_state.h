// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RESOURCE_COORDINATOR_FRAME_TAB_STATE_H_
#define CHROME_BROWSER_RESOURCE_COORDINATOR_FRAME_TAB_STATE_H_

#include <map>
#include <string>

#include "content/public/browser/web_contents_user_data.h"

namespace resource_coordinator {

// User preference metadata, independent of an extension's auto-discard flag.
// Sleeping state remains exclusively in Chromium's native tab lifecycle.
class FrameTabState : public content::WebContentsUserData<FrameTabState> {
 public:
  ~FrameTabState() override = default;
  static constexpr char kNeverSleepKey[] = "frame.never_sleep";
  static FrameTabState* GetOrCreate(content::WebContents* contents);
  static void Restore(content::WebContents* contents,
                      const std::map<std::string, std::string>& extra_data);
  static void Populate(content::WebContents* contents,
                       std::map<std::string, std::string>* extra_data);

  bool never_sleep() const { return never_sleep_; }
  void set_never_sleep(bool value) { never_sleep_ = value; }

 private:
  explicit FrameTabState(content::WebContents* contents);
  friend class content::WebContentsUserData<FrameTabState>;
  bool never_sleep_ = false;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace resource_coordinator

#endif  // CHROME_BROWSER_RESOURCE_COORDINATOR_FRAME_TAB_STATE_H_

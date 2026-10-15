// Copyright 2026 The Ember Authors
// Use of this source code is governed by a BSD-style license.
#ifndef CHROME_APP_FRAME_TYPOGRAPHY_WIN_H_
#define CHROME_APP_FRAME_TYPOGRAPHY_WIN_H_
namespace frame {
// Must run once, in the browser process only, before any Skia font lookup.
void InitializeBrowserTypography();
}
#endif

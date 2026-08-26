#ifndef FRAME_BROWSER_CONTENT_FILTER_H_
#define FRAME_BROWSER_CONTENT_FILTER_H_

#include <cstddef>
#include <string>

#include "shared/tracker_filter.h"

namespace frame::content_filter {

// The process-wide filter list.
//
// ONE engine, loaded once on the UI thread before any browser exists, then
// only ever read. CEF asks about resource loads on the IO thread, so anything
// that could write to the engine after that point would be a data race on
// every request — which is why there is no Reload() here, and why Load() is
// documented as startup-only rather than merely happening to be called there.
//
// The list is read from the profile directory rather than compiled in, so it
// can be replaced with a real maintained list (EasyPrivacy, EasyList) without
// rebuilding Frame. scripts/get-filters.ps1 fetches those.

/// Reads filters.txt from the profile directory. UI thread, startup only.
/// Returns the number of rules that parsed. Missing file is not an error:
/// Frame runs with no blocking rather than refusing to start.
std::size_t Load(const std::string& profile_dir);

/// The engine. Safe to call from any thread once Load() has returned.
const filter::Engine& Get();

/// Whether blocking is applied at all.
///
/// A GATE, not a way to empty the engine. The engine is read from the IO
/// thread on every request and must never be written after startup — clearing
/// its rules to "turn blocking off" would be a data race per resource load —
/// so the switch is an atomic flag checked before the engine is consulted.
/// Settable from the UI thread at any time; readable from any thread.
bool enabled();
void set_enabled(bool value);

/// Total requests blocked since launch, across every tab.
std::size_t total_blocked();
void note_blocked();

}  // namespace frame::content_filter

#endif  // FRAME_BROWSER_CONTENT_FILTER_H_

// Tests for the keyboard shortcut table.
//
// The interesting cases are not "does Ctrl+T mean new tab" — they are the
// near-misses, where a shortcut is one modifier away from a different command
// and a sloppy match would fire the wrong one. Those get the most coverage
// here, because they are the ones that would ship unnoticed.

#include <catch2/catch_test_macros.hpp>

#include <set>

#include "shared/shortcuts.h"

namespace {

using frame::shortcuts::Chord;
using frame::shortcuts::Command;
using frame::shortcuts::IsEditCommand;
using frame::shortcuts::Match;
using frame::shortcuts::SelectedTabIndex;
namespace vk = frame::shortcuts::vk;

Command Ctrl(int key) {
  return Match({key, true, false, false});
}

Command CtrlShift(int key) {
  return Match({key, true, true, false});
}

Command Alt(int key) {
  return Match({key, false, false, true});
}

Command Bare(int key) {
  return Match({key, false, false, false});
}

}  // namespace

TEST_CASE("The chords the user named all resolve", "[shortcuts]") {
  CHECK(Ctrl(vk::kT) == Command::kNewTab);
  CHECK(Ctrl(vk::kW) == Command::kCloseTab);
  CHECK(Ctrl(vk::kR) == Command::kReload);
  CHECK(Ctrl(vk::kTab) == Command::kNextTab);
  CHECK(CtrlShift(vk::kTab) == Command::kPrevTab);
  CHECK(CtrlShift(vk::kN) == Command::kNewIncognitoWindow);
  CHECK(Alt(vk::kLeft) == Command::kBack);
  CHECK(Alt(vk::kRight) == Command::kForward);
  CHECK(Bare(vk::kF11) == Command::kToggleFullscreen);
  CHECK(Ctrl(vk::kJ) == Command::kOpenDownloads);
  CHECK(Ctrl(vk::kI) == Command::kOpenSettings);
  CHECK(Ctrl(vk::kC) == Command::kCopy);
  CHECK(Ctrl(vk::kV) == Command::kPaste);
}

TEST_CASE("Ctrl+1..8 map to strip positions in order", "[shortcuts][tabs]") {
  CHECK(SelectedTabIndex(Ctrl(vk::kDigit1)) == 0);
  CHECK(SelectedTabIndex(Ctrl(vk::kDigit8)) == 7);

  // Every digit in between, so an off-by-one in the arithmetic cannot hide in
  // the untested middle of the range.
  for (int digit = 0; digit < 8; ++digit) {
    CHECK(SelectedTabIndex(Ctrl(vk::kDigit1 + digit)) == digit);
  }

  // 9 is "the last tab", not "the ninth tab" — that is the convention
  // everywhere else, and it is a different command.
  CHECK(Ctrl(vk::kDigit9) == Command::kSelectLastTab);
  CHECK(SelectedTabIndex(Command::kSelectLastTab) == -1);
  CHECK(SelectedTabIndex(Command::kNewTab) == -1);

  // Ctrl+0 belongs to zoom, not to the tab strip.
  CHECK(Ctrl(vk::kDigit0) == Command::kZoomReset);
}

TEST_CASE("Modifiers match exactly, never as a subset", "[shortcuts][modifiers]") {
  // The bug this exists to catch: Shift being ignored, so reopening a closed
  // tab opens a blank one instead.
  CHECK(Ctrl(vk::kT) == Command::kNewTab);
  CHECK(CtrlShift(vk::kT) == Command::kReopenTab);

  CHECK(Ctrl(vk::kW) == Command::kCloseTab);
  CHECK(CtrlShift(vk::kW) == Command::kCloseWindow);

  CHECK(Ctrl(vk::kN) == Command::kNewWindow);
  CHECK(CtrlShift(vk::kN) == Command::kNewIncognitoWindow);

  CHECK(Ctrl(vk::kR) == Command::kReload);
  CHECK(CtrlShift(vk::kR) == Command::kReloadHard);

  CHECK(Ctrl(vk::kI) == Command::kOpenSettings);
  CHECK(CtrlShift(vk::kI) == Command::kDevTools);

  // A bare arrow moves the caret. Only Alt+arrow navigates history.
  CHECK(Bare(vk::kLeft) == Command::kNone);
  CHECK(Ctrl(vk::kLeft) == Command::kNone);
  CHECK(Alt(vk::kLeft) == Command::kBack);

  // Ctrl+Alt combinations are left alone: on many European layouts Ctrl+Alt is
  // how AltGr produces ordinary characters, so claiming them would break
  // typing rather than add a shortcut.
  CHECK(Match({vk::kT, true, false, true}) == Command::kNone);
  CHECK(Match({vk::kW, true, false, true}) == Command::kNone);
  CHECK(Match({vk::kDigit1, true, false, true}) == Command::kNone);
}

TEST_CASE("Alternate chords for the same command all work", "[shortcuts]") {
  CHECK(Ctrl(vk::kF4) == Command::kCloseTab);
  CHECK(Bare(vk::kF5) == Command::kReload);
  CHECK(Ctrl(vk::kF5) == Command::kReloadHard);
  CHECK(Ctrl(vk::kPageDown) == Command::kNextTab);
  CHECK(Ctrl(vk::kPageUp) == Command::kPrevTab);
  CHECK(Ctrl(vk::kL) == Command::kFocusAddress);
  CHECK(Alt(vk::kD) == Command::kFocusAddress);
  CHECK(Bare(vk::kF6) == Command::kFocusAddress);
  CHECK(Ctrl(vk::kOemComma) == Command::kOpenSettings);

  // Number row and numpad have to agree, or zoom works on one keyboard half
  // and not the other.
  CHECK(Ctrl(vk::kOemPlus) == Command::kZoomIn);
  CHECK(Ctrl(vk::kAdd) == Command::kZoomIn);
  CHECK(CtrlShift(vk::kOemPlus) == Command::kZoomIn);
  CHECK(Ctrl(vk::kOemMinus) == Command::kZoomOut);
  CHECK(Ctrl(vk::kSubtract) == Command::kZoomOut);

  CHECK(Ctrl(vk::kZ) == Command::kUndo);
  CHECK(Ctrl(vk::kY) == Command::kRedo);
  CHECK(CtrlShift(vk::kZ) == Command::kRedo);
}

TEST_CASE("Unmapped chords stay unmapped", "[shortcuts]") {
  CHECK(Bare(vk::kA) == Command::kNone);
  CHECK(Bare(vk::kTab) == Command::kNone);
  CHECK(Match({0, false, false, false}) == Command::kNone);
  CHECK(Match({0, true, true, true}) == Command::kNone);

  // Not claimed on purpose: find-in-page has no UI yet, and binding Ctrl+F to
  // nothing is worse than leaving it free.
  CHECK(Ctrl(0x46 /* 'F' */) == Command::kNone);
}

TEST_CASE("The editing group is identified as such", "[shortcuts][editing]") {
  CHECK(IsEditCommand(Command::kCopy));
  CHECK(IsEditCommand(Command::kCut));
  CHECK(IsEditCommand(Command::kPaste));
  CHECK(IsEditCommand(Command::kSelectAll));
  CHECK(IsEditCommand(Command::kUndo));
  CHECK(IsEditCommand(Command::kRedo));

  // Everything the page must keep handling itself is outside the group.
  CHECK_FALSE(IsEditCommand(Command::kNone));
  CHECK_FALSE(IsEditCommand(Command::kNewTab));
  CHECK_FALSE(IsEditCommand(Command::kZoomReset));
  CHECK_FALSE(IsEditCommand(Command::kPrint));
}

TEST_CASE("No chord is bound twice", "[shortcuts][table]") {
  // Two rows for one chord would make the table's meaning depend on its order,
  // and the second binding would be silently dead.
  std::set<int> seen;
  for (const auto& binding : frame::shortcuts::internal::kBindings) {
    const int fingerprint = (binding.key << 3) | (binding.ctrl ? 4 : 0) |
                            (binding.shift ? 2 : 0) | (binding.alt ? 1 : 0);
    INFO("duplicate binding for virtual key " << binding.key);
    CHECK(seen.insert(fingerprint).second);
  }
}

TEST_CASE("The computed Ctrl+digit range does not collide with the table",
          "[shortcuts][table]") {
  // Ctrl+1..8 short-circuit before the table is consulted, so a row for one of
  // them would never be reached.
  for (const auto& binding : frame::shortcuts::internal::kBindings) {
    const bool in_digit_range =
        binding.key >= vk::kDigit1 && binding.key <= vk::kDigit8;
    const bool shadowed = in_digit_range && binding.ctrl && !binding.shift &&
                          !binding.alt;
    INFO("virtual key " << binding.key << " is already handled by the "
                           "computed Ctrl+1..8 range");
    CHECK_FALSE(shadowed);
  }
}

// Frame — the keyboard shortcut table.
//
// Framework-free on purpose, for the same reason chrome_layout.h is: the chord
// -> command mapping is the part that is easy to get subtly wrong, and it
// should be testable without a running browser. The specific mistakes this
// guards against are all near-misses rather than typos:
//
//   - Ctrl+Shift+T silently matching the Ctrl+T rule, so reopening a closed
//     tab opens a blank one instead
//   - Alt+Left matching a bare Left, so the browser goes back while the caret
//     is only meant to move in a text field
//   - the numpad +/- not matching their number-row twins
//
// Matching is therefore EXACT on all three modifiers, never a subset test.
//
// Virtual-key codes are restated below rather than pulled from <windows.h>:
// nothing in shared/ may drag a platform header into the test binary. The
// values are fixed by the Win32 ABI and cannot drift.

#ifndef FRAME_SHARED_SHORTCUTS_H_
#define FRAME_SHARED_SHORTCUTS_H_

namespace frame::shortcuts {

namespace vk {

inline constexpr int kTab = 0x09;
inline constexpr int kEscape = 0x1B;
inline constexpr int kPageUp = 0x21;
inline constexpr int kPageDown = 0x22;
inline constexpr int kHome = 0x24;
inline constexpr int kLeft = 0x25;
inline constexpr int kRight = 0x27;

// '0'..'9' and 'A'..'Z' are contiguous and equal to their ASCII codes.
inline constexpr int kDigit0 = 0x30;
inline constexpr int kDigit1 = 0x31;
inline constexpr int kDigit8 = 0x38;
inline constexpr int kDigit9 = 0x39;
inline constexpr int kA = 0x41;
inline constexpr int kB = 0x42;
inline constexpr int kC = 0x43;
inline constexpr int kD = 0x44;
inline constexpr int kH = 0x48;
inline constexpr int kI = 0x49;
inline constexpr int kJ = 0x4A;
inline constexpr int kL = 0x4C;
inline constexpr int kN = 0x4E;
inline constexpr int kO = 0x4F;
inline constexpr int kP = 0x50;
inline constexpr int kR = 0x52;
inline constexpr int kT = 0x54;
inline constexpr int kV = 0x56;
inline constexpr int kW = 0x57;
inline constexpr int kX = 0x58;
inline constexpr int kY = 0x59;
inline constexpr int kZ = 0x5A;

inline constexpr int kAdd = 0x6B;       // numpad +
inline constexpr int kSubtract = 0x6D;  // numpad -

inline constexpr int kF4 = 0x73;
inline constexpr int kF5 = 0x74;
inline constexpr int kF6 = 0x75;
inline constexpr int kF11 = 0x7A;
inline constexpr int kF12 = 0x7B;

inline constexpr int kOemPlus = 0xBB;   // '+'/'=' on the number row
inline constexpr int kOemComma = 0xBC;
inline constexpr int kOemMinus = 0xBD;  // '-'/'_' on the number row

}  // namespace vk

// What a chord asks the browser to do.
//
// Ordering matters in exactly two places: kSelectTab1..kSelectTab8 must stay
// contiguous and in order, and the editing group must stay contiguous, because
// SelectedTabIndex() and IsEditCommand() do arithmetic on them.
enum class Command {
  kNone = 0,

  // Tabs.
  kNewTab,
  kCloseTab,
  kReopenTab,
  kNextTab,
  kPrevTab,
  kSelectTab1,
  kSelectTab2,
  kSelectTab3,
  kSelectTab4,
  kSelectTab5,
  kSelectTab6,
  kSelectTab7,
  kSelectTab8,
  kSelectLastTab,

  // Windows.
  kNewWindow,
  kNewIncognitoWindow,
  kCloseWindow,
  kToggleFullscreen,

  // Navigation.
  kBack,
  kForward,
  kReload,
  kReloadHard,
  kStop,
  kHomePage,

  // Chrome.
  kFocusAddress,
  kToggleSidebar,
  kBookmarkPage,
  kOpenDownloads,
  kOpenHistory,
  kOpenBookmarks,
  kOpenSettings,
  kDevTools,
  kPrint,

  // Zoom.
  kZoomIn,
  kZoomOut,
  kZoomReset,

  // Editing. Only ever acted on for an off-screen chrome surface — a real page
  // is a native child window and Chromium already implements these there, so
  // intercepting them would replace a working implementation with our own.
  kCopy,
  kCut,
  kPaste,
  kSelectAll,
  kUndo,
  kRedo,
};

struct Chord {
  int key = 0;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

namespace internal {

struct Binding {
  int key;
  bool ctrl;
  bool shift;
  bool alt;
  Command command;
};

// The whole table, in one place.
//
// Where a shortcut has more than one conventional chord, every one of them is
// listed rather than picking a favourite: Ctrl+W and Ctrl+F4 both close a tab
// everywhere else, and a browser that honours only one of them feels broken.
inline constexpr Binding kBindings[] = {
    // --- tabs ---
    {vk::kT, true, false, false, Command::kNewTab},
    {vk::kT, true, true, false, Command::kReopenTab},
    {vk::kW, true, false, false, Command::kCloseTab},
    {vk::kF4, true, false, false, Command::kCloseTab},
    {vk::kTab, true, false, false, Command::kNextTab},
    {vk::kTab, true, true, false, Command::kPrevTab},
    {vk::kPageDown, true, false, false, Command::kNextTab},
    {vk::kPageUp, true, false, false, Command::kPrevTab},
    {vk::kDigit9, true, false, false, Command::kSelectLastTab},

    // --- windows ---
    {vk::kN, true, false, false, Command::kNewWindow},
    {vk::kN, true, true, false, Command::kNewIncognitoWindow},
    {vk::kW, true, true, false, Command::kCloseWindow},
    {vk::kF11, false, false, false, Command::kToggleFullscreen},

    // --- navigation ---
    {vk::kLeft, false, false, true, Command::kBack},
    {vk::kRight, false, false, true, Command::kForward},
    {vk::kR, true, false, false, Command::kReload},
    {vk::kF5, false, false, false, Command::kReload},
    {vk::kR, true, true, false, Command::kReloadHard},
    {vk::kF5, true, false, false, Command::kReloadHard},
    {vk::kEscape, false, false, false, Command::kStop},
    {vk::kHome, false, false, true, Command::kHomePage},

    // --- chrome ---
    {vk::kL, true, false, false, Command::kFocusAddress},
    {vk::kD, false, false, true, Command::kFocusAddress},
    {vk::kF6, false, false, false, Command::kFocusAddress},
    {vk::kB, true, false, false, Command::kToggleSidebar},
    {vk::kD, true, false, false, Command::kBookmarkPage},
    {vk::kJ, true, false, false, Command::kOpenDownloads},
    {vk::kO, true, true, false, Command::kOpenBookmarks},
    {vk::kH, true, false, false, Command::kOpenHistory},
    {vk::kI, true, false, false, Command::kOpenSettings},
    {vk::kOemComma, true, false, false, Command::kOpenSettings},
    {vk::kF12, false, false, false, Command::kDevTools},
    {vk::kI, true, true, false, Command::kDevTools},
    {vk::kP, true, false, false, Command::kPrint},

    // --- zoom ---
    // Both the number row and the numpad, plus Ctrl+Shift+'=', because that is
    // what actually reaches us when someone "presses Ctrl and plus" on a US
    // layout without touching the numpad.
    {vk::kOemPlus, true, false, false, Command::kZoomIn},
    {vk::kOemPlus, true, true, false, Command::kZoomIn},
    {vk::kAdd, true, false, false, Command::kZoomIn},
    {vk::kOemMinus, true, false, false, Command::kZoomOut},
    {vk::kSubtract, true, false, false, Command::kZoomOut},
    {vk::kDigit0, true, false, false, Command::kZoomReset},

    // --- editing ---
    {vk::kC, true, false, false, Command::kCopy},
    {vk::kX, true, false, false, Command::kCut},
    {vk::kV, true, false, false, Command::kPaste},
    {vk::kA, true, false, false, Command::kSelectAll},
    {vk::kZ, true, false, false, Command::kUndo},
    {vk::kY, true, false, false, Command::kRedo},
    {vk::kZ, true, true, false, Command::kRedo},
};

}  // namespace internal

/// The chord -> command lookup. Unmapped chords return kNone.
///
/// Ctrl+1 through Ctrl+8 are computed rather than tabulated, so the digits and
/// the commands cannot fall out of step with each other.
inline Command Match(const Chord& chord) {
  if (chord.ctrl && !chord.shift && !chord.alt && chord.key >= vk::kDigit1 &&
      chord.key <= vk::kDigit8) {
    return static_cast<Command>(static_cast<int>(Command::kSelectTab1) +
                                (chord.key - vk::kDigit1));
  }

  for (const internal::Binding& binding : internal::kBindings) {
    // Exact on every modifier. A subset test is what lets Ctrl+Shift+T fire
    // the Ctrl+T rule.
    if (binding.key == chord.key && binding.ctrl == chord.ctrl &&
        binding.shift == chord.shift && binding.alt == chord.alt) {
      return binding.command;
    }
  }
  return Command::kNone;
}

/// 0-based strip position for kSelectTab1..kSelectTab8, or -1 for anything
/// else. kSelectLastTab is deliberately NOT included: "the last tab" depends on
/// how many tabs exist, which this header has no business knowing.
inline int SelectedTabIndex(Command command) {
  if (command < Command::kSelectTab1 || command > Command::kSelectTab8) {
    return -1;
  }
  return static_cast<int>(command) - static_cast<int>(Command::kSelectTab1);
}

/// True for the clipboard and undo group, which the two call sites treat
/// differently — see the comment on the enum.
inline bool IsEditCommand(Command command) {
  return command >= Command::kCopy && command <= Command::kRedo;
}

}  // namespace frame::shortcuts

#endif  // FRAME_SHARED_SHORTCUTS_H_

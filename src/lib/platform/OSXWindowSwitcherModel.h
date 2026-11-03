/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace deskflow::osx {

//! Process id of a running application.
using SwitcherPid = int32_t;
//! CGWindowID of a window (0 = unknown).
using SwitcherWindowId = uint32_t;

//! Most-recently-used order of applications and windows (pure logic).
/*!
The platform layer reports app activations and focused windows; both lists are
bounded so a long running client cannot grow them without limit.
*/
class SwitcherMru
{
public:
  static constexpr size_t kMaxEntries = 200;

  //! \p pid became the active application.
  void touchApp(SwitcherPid pid);
  //! \p windowId of \p pid became the focused window (also touches the app).
  void touchWindow(SwitcherPid pid, SwitcherWindowId windowId);
  //! \p pid terminated.
  void removeApp(SwitcherPid pid);
  //! Drops windows that no longer exist.
  void retainWindows(const std::vector<SwitcherWindowId> &alive);

  //! 0 = most recent; no value if unknown.
  std::optional<size_t> appRank(SwitcherPid pid) const;
  //! 0 = most recent; no value if unknown.
  std::optional<size_t> windowRank(SwitcherWindowId windowId) const;

private:
  std::vector<SwitcherPid> m_apps;
  std::vector<SwitcherWindowId> m_windows;
};

//! Selection when the switcher opens: the previous window (or the last one if \p reverse).
size_t switcherInitialSelection(size_t count, bool reverse);

//! Selection after moving \p delta steps from \p current, wrapping around.
size_t switcherStep(size_t current, size_t count, int delta);

//! Rectangle in global display points (top-left origin, as reported by Accessibility and ScreenCaptureKit).
struct SwitcherRect
{
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

//! Kind of a Space as reported by the window server.
enum class SwitcherSpaceKind : uint8_t
{
  Desktop,    //!< a Mission Control desktop
  Fullscreen, //!< a fullscreen app or a split view of two
  Other,      //!< a system Space; not shown and not a Control+Arrow step
};

//! A window of a fullscreen Space (one per tile of a split view).
struct SwitcherTile
{
  SwitcherPid pid = 0;
  SwitcherWindowId windowId = 0;
};

//! A Space of one display in Mission Control order (pure data).
struct SwitcherSpaceEntry
{
  uint64_t id = 0;
  SwitcherSpaceKind kind = SwitcherSpaceKind::Other;
  std::vector<SwitcherTile> tiles; //!< windows of a fullscreen Space, left to right
};

//! One item of the Spaces strip (the top bar of Mission Control).
struct SwitcherStripItem
{
  uint64_t spaceId = 0;
  SwitcherSpaceKind kind = SwitcherSpaceKind::Desktop;
  int number = 0;  //!< "Desktop N" of Mission Control; 0 if the display has a single desktop (no number)
  int steps = 0;   //!< Control+Right presses from the current Space (negative: Control+Left presses)
  bool current = false;
  std::vector<SwitcherTile> tiles; //!< windows of a fullscreen Space
};

//! The Spaces strip of one display: desktops and fullscreen Spaces in Mission Control order.
/*!
Desktops are numbered 1..N only if there are several (Mission Control shows a single one as
"Desktop"). Other Spaces are skipped. Yields nothing if \p current is not a desktop or
fullscreen Space of \p spaces, since the steps would be unknown.
*/
std::vector<SwitcherStripItem> switcherSpaceStrip(const std::vector<SwitcherSpaceEntry> &spaces, uint64_t current);

//! What the closed-loop Space switch does next (see switcherPlanSpaceStep).
struct SwitcherSpaceStepPlan
{
  enum class Action : uint8_t
  {
    Done,  //!< the current Space is the target
    Step,  //!< post one Control+Arrow in \c direction, then wait for \c expected to become current
    Abort, //!< the Spaces are unknown or someone else switched Spaces meanwhile
  };
  Action action = Action::Abort;
  int direction = 0;     //!< +1: Control+Right, -1: Control+Left (Step only)
  int remaining = 0;     //!< steps left to the target, this one included (Step only)
  uint64_t expected = 0; //!< the Space that must become current after this step (Step only)
};

//! Next step of a Space switch, from the Space that is current right now.
/*!
\p order: Space ids of the display in Control+Arrow order (the strip order). \p previousExpected: the
Space the previous step was to reach, 0 before the first step; if \p current differs from it, the user
or the system switched meanwhile and the switch aborts. The distance is recomputed from \p current on
every call, so a lost or a doubled step never makes the switch go past the target.
*/
SwitcherSpaceStepPlan
switcherPlanSpaceStep(const std::vector<uint64_t> &order, uint64_t current, uint64_t target, uint64_t previousExpected);

//! Where a Space switch stands, given the reported current Space (see switcherSpaceStepPosition).
struct SwitcherSpacePosition
{
  enum class Status : uint8_t
  {
    Confirmed, //!< the reported current Space is where the posted steps lead
    Lagging,   //!< the reported current Space is still on the way there (it updates late)
    Elsewhere, //!< the reported current Space is off the way: someone else switched Spaces
  };
  Status status = Status::Elsewhere;
  uint64_t position = 0; //!< the Space to plan the next step from (Confirmed and Lagging only)
};

//! Position of a Space switch whose steps lead from \p confirmed to \p assumed (pure).
/*!
\p order: Space ids in Control+Arrow order. \p confirmed: the last Space reported current by the switch,
\p assumed: the Space its posted steps lead to (both 0 before the first step). The current Space is
reported only after a transition ended (about a second after a step), so a \p current between
\p confirmed and \p assumed is still a valid position: the switch goes on from \p assumed.
*/
SwitcherSpacePosition
switcherSpaceStepPosition(const std::vector<uint64_t> &order, uint64_t current, uint64_t confirmed, uint64_t assumed);

//! Timing of the stepping Space switch, in seconds.
struct SwitcherStepTiming
{
  double minGap = 0.25;  //!< at least this long between two steps
  double settle = 0.6;   //!< a transition is taken as running this long after a step or direct method
  double trust = 1.5;    //!< after this the reported current Space is taken as real, even if late
  double limit = 3.0;    //!< longest wait for anything (a stuck transition flag must not stall the switch)
};

//! What the stepping Space switch observes on one poll after a step.
struct SwitcherStepObservation
{
  double sincePost = 0;      //!< since the last step was posted
  bool landed = false;       //!< the reported current Space is where the posted steps lead
  bool animating = false;    //!< the display shows a Space transition
  bool sawAnimation = false; //!< a transition was seen since the last step
};

//! When the stepping Space switch posts its next step (see switcherStepGate).
enum class SwitcherStepGate : uint8_t
{
  Wait,   //!< the last transition is still running or not known to be over: poll again
  Ready,  //!< the last transition is over: plan the next step from the assumed position
  Resync, //!< nothing more to wait for: take the reported current Space as real and plan from it
};

//! Whether the last step's transition is over (pure).
/*!
A step is over once the reported current Space is where it leads, or once a transition was seen and
has ended (the current Space is reported later), never before \c minGap. Without either, the switch
waits until \c trust and then believes the reported current Space.
*/
SwitcherStepGate switcherStepGate(const SwitcherStepObservation &now, const SwitcherStepTiming &timing = {});

//! What a new Space switch observes before it reads the current Space.
struct SwitcherSettleObservation
{
  double sinceStart = 0;    //!< since the new switch was asked for
  double sinceAction = -1;  //!< since the last step or direct method of any switch, negative if none
  bool animating = false;   //!< the display shows a Space transition
  bool pendingLanded = true; //!< the steps of a replaced switch have shown up in the current Space
};

//! Whether the Spaces are settled, so a new switch can read the current Space and start (pure).
bool switcherSpacesSettled(const SwitcherSettleObservation &now, const SwitcherStepTiming &timing = {});

//! A way to jump straight to a window's Space (one transition) instead of stepping.
enum class SwitcherDirectMethod : uint8_t
{
  Accessibility, //!< "A": app frontmost, window main + focused, AXRaise
  Activation,    //!< "B": NSRunningApplication activation of the owning app, then A
  WindowServer,  //!< "C": window server front process + make-key event records, then AXRaise
};

//! The letter of \p method in the log and in the order setting: "A", "B" or "C".
const char *switcherDirectMethodName(SwitcherDirectMethod method);

//! The methods tried by default, in this order.
std::vector<SwitcherDirectMethod> switcherDefaultDirectMethods();

//! The order given by \p spec (letters A, B, C, case-insensitive; other characters separate them).
/*!
Unknown letters and repeats are skipped. Empty or nothing valid yields the default order; "-" or
"none" yields no methods (always step).
*/
std::vector<SwitcherDirectMethod> switcherParseDirectMethods(const std::string &spec);

//! \p order with \p method moved to the front (the one that worked is tried first next time).
std::vector<SwitcherDirectMethod>
switcherPromoteDirectMethod(const std::vector<SwitcherDirectMethod> &order, SwitcherDirectMethod method);

//! Timing of the direct Space switch, in seconds.
struct SwitcherDirectTiming
{
  double perMethod = 0.35;      //!< wait for a transition to start after a method
  double total = 1.05;          //!< no new method after this (stepping starts instead)
  double animationLimit = 2.0;  //!< longest wait for a transition that has started
  double settle = 0.25;         //!< wait after the animation ended for the current Space to update
};

//! What the direct Space switch observes on one poll.
struct SwitcherDirectObservation
{
  uint64_t current = 0;       //!< current Space of the display, 0 if unknown
  bool animating = false;     //!< the display shows a Space transition
  double sinceStart = 0;      //!< since the direct switch started
  double sinceMethod = 0;     //!< since the last method was tried (ignored before the first)
  double sinceAnimation = -1; //!< since a transition was last seen, negative if never (this method)
  bool canRetarget = false;   //!< after a wrong landing the exact window may be tried once more
  bool retargeted = false;    //!< the exact window was tried after a wrong landing (\c from is that Space)
};

//! What the direct Space switch does next (see switcherPlanDirectSwitch).
struct SwitcherDirectPlan
{
  enum class Action : uint8_t
  {
    Try,      //!< try \c method, then poll again
    Wait,     //!< poll again later
    Arrived,  //!< the target is current
    Retarget, //!< landed on another Space (another window of the app): focus the exact window once more
    FallBack, //!< every method failed, time is up or the Spaces moved elsewhere: step from the current Space
  };
  Action action = Action::FallBack;
  SwitcherDirectMethod method = SwitcherDirectMethod::Accessibility; //!< Try only
};

//! Next action of the direct Space switch from \p from to \p target (pure).
/*!
\p tried methods of \p order were tried so far. A method counts as working once a transition
starts (\c animating) or the target becomes current; then it is waited for, not tried again.
If a method lands on another Space (say another fullscreen window of the same app), the exact window
is tried once more (\c Retarget, if \c canRetarget) from there; if that fails too, stepping starts.
*/
SwitcherDirectPlan switcherPlanDirectSwitch(
    const std::vector<SwitcherDirectMethod> &order, size_t tried, uint64_t from, uint64_t target,
    const SwitcherDirectObservation &now, const SwitcherDirectTiming &timing = {}
);

//! Id of the first "Switch to Desktop N" symbolic hotkey of macOS (Desktop 1; Desktop N is this + N - 1).
inline constexpr int kSwitcherDesktopHotkeyFirst = 118;
//! Number of "Switch to Desktop N" symbolic hotkeys (Desktop 1 to 16).
inline constexpr int kSwitcherDesktopHotkeyCount = 16;

//! A key combination of a macOS symbolic hotkey.
struct SwitcherHotkey
{
  uint16_t keyCode = 0;   //!< virtual key code
  uint64_t modifiers = 0; //!< CGEventFlags / NSEventModifierFlags bits (Shift, Control, Option, Command, fn)
  bool operator==(const SwitcherHotkey &other) const = default;
};

//! Symbolic hotkey id of "Switch to Desktop \p number", or 0 if there is none for that number.
int switcherDesktopHotkeyId(int number);

//! The key combination of a symbolic hotkey entry (com.apple.symbolichotkeys, AppleSymbolicHotKeys).
/*!
\p enabled and \p parameters are the entry's "enabled" and "value.parameters" ([character, key code,
modifiers]). None if disabled, malformed, without a key, or without a modifier (a bare key would type
into the focused app if macOS ignored it).
*/
std::optional<SwitcherHotkey> switcherParseSymbolicHotkey(bool enabled, const std::vector<int64_t> &parameters);

//! "Desktop N" of Space \p spaceId among \p entries (one display, Mission Control order): its 1-based
//! position among the desktops, fullscreen Spaces not counted; 0 if it is not a desktop there.
int switcherDesktopNumber(const std::vector<SwitcherSpaceEntry> &entries, uint64_t spaceId);

//! Timing of the switch by the "Switch to Desktop N" hotkey, in seconds.
struct SwitcherHotkeyTiming
{
  double timeout = 1.6;        //!< no change of the current Space by then: the hotkey did nothing
  double animationLimit = 3.0; //!< longest wait while a transition is seen
};

//! What the hotkey switch observes on one poll.
struct SwitcherHotkeyObservation
{
  uint64_t current = 0;       //!< current Space of the display, 0 if unknown
  bool animating = false;     //!< the display shows a Space transition
  double sinceStart = 0;      //!< since the hotkey was posted
};

//! What the hotkey switch does next (see switcherPlanHotkeySwitch).
enum class SwitcherHotkeyPlan : uint8_t
{
  Wait,     //!< poll again
  Arrived,  //!< the target is current
  Elsewhere, //!< another Space became current: go on by stepping from there
  NoEffect, //!< nothing happened in time: try the other ways
};

//! Next action of the switch by hotkey from \p from to \p target (pure).
/*!
The current Space is reported only when the transition ends (about a second after a keyboard switch),
so the switch waits for it up to \c timeout, longer while a transition is seen.
*/
SwitcherHotkeyPlan switcherPlanHotkeySwitch(
    uint64_t from, uint64_t target, const SwitcherHotkeyObservation &now, const SwitcherHotkeyTiming &timing = {}
);

//! Names of one window of a fullscreen Space.
struct SwitcherTileName
{
  SwitcherPid pid = 0;
  std::string appName;
  std::string windowTitle; //!< may be empty
};

//! Titles of fullscreen Spaces (\p spaces: names of their tiles), like Mission Control.
/*!
A Space is titled by its app name, or by its window title if the same app has several
fullscreen Spaces (falling back to the app name if the title is unknown). Split views
join their tiles with " & ".
*/
std::vector<std::string> switcherFullscreenTitles(const std::vector<std::vector<SwitcherTileName>> &spaces);

//! Title of the strip item of a desktop (translated): "Desktop 2", or "Desktop" for \p number 0.
std::string switcherDesktopTitle(int number);

//! Label of the current Space in the strip (translated).
std::string switcherCurrentSpaceLabel();

//! An item of the switcher as seen by the initial selection.
struct SwitcherSelectable
{
  std::optional<size_t> rank; //!< window MRU rank (best of its windows); none for desktops and unknown windows
  bool current = false;       //!< the current Space or the focused window: switching there does nothing
  bool window = false;        //!< a grid window, not a strip item
};

//! Selection when the switcher opens over \p items (strip first, then windows).
/*!
\p reverse selects the last item. Otherwise the most recently used item that is not current
(so Alt+Tab goes back to the previous fullscreen app or window, wherever it is); without MRU
ranks the first window that is not current, else the strip item after the current one.
*/
size_t switcherInitialSelection(const std::vector<SwitcherSelectable> &items, bool reverse);

//! Size of a remote Accessibility element token.
inline constexpr size_t kSwitcherRemoteTokenSize = 20;

//! Token for creating the Accessibility element \p elementId of process \p pid remotely.
/*!
Layout (native byte order): pid (int32), 0 (int32), the "coco" magic
0x636f636f (int32), element id (uint64).
*/
std::array<uint8_t, kSwitcherRemoteTokenSize> switcherRemoteToken(SwitcherPid pid, uint64_t elementId);

//! Size of a window server event record.
inline constexpr size_t kSwitcherEventRecordSize = 0xf8;

//! Event record that makes \p windowId the key window of its process (\p press: first half, else second half).
std::array<uint8_t, kSwitcherEventRecordSize> switcherMakeKeyWindowEvent(SwitcherWindowId windowId, bool press);

//! Size of a thumbnail image in pixels; 0 x 0 if nothing should be captured.
struct SwitcherPixelSize
{
  int width = 0;
  int height = 0;
  bool operator==(const SwitcherPixelSize &other) const = default;
};

//! Pixel size of a thumbnail of a \p window (points) aspect-fit into \p box (points) at the screen \p scale.
/*!
Never larger than the window itself at \p scale (no upscaling) and never wider
than \p maxPixelWidth. Empty or invalid input yields 0 x 0.
*/
SwitcherPixelSize switcherThumbnailSize(SwitcherRect window, SwitcherRect box, double scale, int maxPixelWidth);

//! A window as listed by the capture API (pure data).
struct SwitcherCaptureWindow
{
  SwitcherWindowId windowId = 0;
  SwitcherPid pid = 0;
  std::string title; //!< UTF-8, may be empty
  SwitcherRect frame;
};

//! Index into \p windows of the capture window showing the switcher item \p item.
/*!
An item with a known window id matches that id only. Otherwise a window of the
same process that is not in \p claimed is chosen by title and frame (within 2
points): title and frame, else a unique title, else a unique frame. Ambiguous
or missing matches yield no value, so the item keeps its icon.
*/
std::optional<size_t> matchSwitcherWindow(
    const SwitcherCaptureWindow &item, const std::vector<SwitcherCaptureWindow> &windows,
    const std::vector<SwitcherWindowId> &claimed
);

//! Item indices to capture thumbnails for: \p selection first, then the list from the top, at most \p limit.
std::vector<size_t> switcherCaptureOrder(size_t count, size_t selection, size_t limit);

//! Small most-recently-used cache (e.g. last thumbnails per window id); linear, meant for a few dozen entries.
template <typename Key, typename Value> class SwitcherLru
{
public:
  explicit SwitcherLru(size_t capacity) : m_capacity(capacity)
  {
  }

  //! Stores \p value as the most recent entry, evicting the least recent one if full.
  void put(const Key &key, Value value)
  {
    erase(key);
    if (m_capacity == 0) {
      return;
    }
    m_entries.insert(m_entries.begin(), {key, std::move(value)});
    if (m_entries.size() > m_capacity) {
      m_entries.pop_back();
    }
  }

  //! The value for \p key (made most recent), or nullptr.
  const Value *find(const Key &key)
  {
    for (size_t i = 0; i < m_entries.size(); ++i) {
      if (m_entries[i].first == key) {
        if (i != 0) {
          auto entry = std::move(m_entries[i]);
          m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(i));
          m_entries.insert(m_entries.begin(), std::move(entry));
        }
        return &m_entries.front().second;
      }
    }
    return nullptr;
  }

  void erase(const Key &key)
  {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
      if (it->first == key) {
        m_entries.erase(it);
        return;
      }
    }
  }

  void clear()
  {
    m_entries.clear();
  }

  size_t size() const
  {
    return m_entries.size();
  }

private:
  size_t m_capacity;
  std::vector<std::pair<Key, Value>> m_entries; //!< most recent first
};

} // namespace deskflow::osx

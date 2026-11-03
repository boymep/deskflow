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

//! One window offered by the built-in window switcher (pure data, no AppKit).
struct SwitcherCandidate
{
  SwitcherPid pid = 0;
  SwitcherWindowId windowId = 0; //!< 0 if the CGWindowID could not be resolved
  int appWindowIndex = 0;        //!< position in the app's Accessibility window list (front to back)
  bool minimized = false;
  bool otherSpace = false; //!< on a Space (desktop or fullscreen app) that is not shown right now
};

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

//! Order of \p candidates (indices) as shown by the switcher, most recent first.
/*!
Windows on screen come first in window server stacking order \p zOrder (front to
back), which is their real usage order on the current Space. The others
(minimized, hidden apps) follow by window MRU rank, then app MRU rank, then
their position in the app's window list.

Windows on other Spaces are mixed into the on-screen ones by window MRU rank,
like Windows does: one goes before an on-screen window if it was used more
recently than that window and every window behind it (stacking order implies
the window in front was used later). The frontmost window always stays first.
Without a window MRU rank they follow the on-screen windows like the others.
*/
std::vector<size_t> orderSwitcherCandidates(
    const std::vector<SwitcherCandidate> &candidates, const std::vector<SwitcherWindowId> &zOrder,
    const SwitcherMru &mru
);

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

//! A window as listed by the window server, with the Spaces it is on (pure data).
struct SwitcherSpaceWindow
{
  SwitcherWindowId windowId = 0;
  SwitcherPid pid = 0;
  int layer = 0;
  double alpha = 1.0;
  SwitcherRect frame;
  std::vector<uint64_t> spaces; //!< Space ids; empty if unknown
};

//! Smallest width and height, in points, of a window from another Space worth listing.
inline constexpr double kSwitcherMinSpaceWindowSize = 100.0;
//! Largest width to height ratio (either way) of such a window; flatter ones are toolbar or tab strips.
inline constexpr double kSwitcherMaxSpaceWindowAspect = 6.0;

//! True if \p window may be a user window: normal level, visible, sensible size and shape, of an \p allowed process.
/*!
Checks everything except the Spaces, so it can run before they are looked up.
*/
bool switcherSpaceWindowPlausible(const SwitcherSpaceWindow &window, const std::vector<SwitcherPid> &allowed);

//! Indices of \p windows to add to the switcher as windows on other Spaces, in input order, at most \p limit.
/*!
Keeps plausible windows (see switcherSpaceWindowPlausible) whose window id is
not in \p known (already listed via Accessibility), whose Spaces are all in
\p userSpaces (desktops and fullscreen apps; drops windows of system Spaces or
on no Space) and none of them in \p visibleSpaces (a window there that
Accessibility does not report is not a real window). Duplicates are dropped.
*/
std::vector<size_t> selectOtherSpaceWindows(
    const std::vector<SwitcherSpaceWindow> &windows, const std::vector<SwitcherPid> &allowed,
    const std::vector<SwitcherWindowId> &known, const std::vector<uint64_t> &userSpaces,
    const std::vector<uint64_t> &visibleSpaces, size_t limit
);

//! A Space of one display in Mission Control order (pure data).
struct SwitcherSpaceEntry
{
  uint64_t id = 0;
  bool desktop = false; //!< a Mission Control desktop; false for a fullscreen app or another kind of Space
};

//! An empty desktop the switcher can jump to.
struct SwitcherDesktop
{
  uint64_t spaceId = 0;
  int number = 0; //!< "Desktop N" of Mission Control, counted from 1 over the desktops of the display
  int steps = 0;  //!< Control+Right presses from the current Space (negative: Control+Left presses)
};

//! Empty desktops of one display, except the \p current Space, in Mission Control order.
/*!
\p spaces are all Spaces of the display in order (desktops are numbered 1..N, fullscreen
apps are not numbered but still count as a step). A desktop is empty if its id is not in
\p occupied. Yields nothing if \p current is not among \p spaces.
*/
std::vector<SwitcherDesktop> switcherEmptyDesktops(
    const std::vector<SwitcherSpaceEntry> &spaces, uint64_t current, const std::vector<uint64_t> &occupied
);

//! Title of the switcher item of desktop \p number (translated, "Desktop 2").
std::string switcherDesktopTitle(int number);

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

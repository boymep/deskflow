/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

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
(minimized, hidden apps, other Spaces) follow by window MRU rank, then app MRU
rank, then their position in the app's window list.
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

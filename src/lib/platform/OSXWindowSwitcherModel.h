/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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

} // namespace deskflow::osx

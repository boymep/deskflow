/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXWindowSwitcherModel.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace deskflow::osx {

namespace {

template <typename T> void moveToFront(std::vector<T> &list, T value, size_t cap)
{
  if (const auto it = std::find(list.begin(), list.end(), value); it != list.end()) {
    list.erase(it);
  }
  list.insert(list.begin(), value);
  if (list.size() > cap) {
    list.resize(cap);
  }
}

template <typename T> std::optional<size_t> rankOf(const std::vector<T> &list, T value)
{
  if (const auto it = std::find(list.begin(), list.end(), value); it != list.end()) {
    return static_cast<size_t>(it - list.begin());
  }
  return std::nullopt;
}

} // namespace

void SwitcherMru::touchApp(SwitcherPid pid)
{
  moveToFront(m_apps, pid, kMaxEntries);
}

void SwitcherMru::touchWindow(SwitcherPid pid, SwitcherWindowId windowId)
{
  touchApp(pid);
  if (windowId != 0) {
    moveToFront(m_windows, windowId, kMaxEntries);
  }
}

void SwitcherMru::removeApp(SwitcherPid pid)
{
  m_apps.erase(std::remove(m_apps.begin(), m_apps.end(), pid), m_apps.end());
}

void SwitcherMru::retainWindows(const std::vector<SwitcherWindowId> &alive)
{
  m_windows.erase(
      std::remove_if(
          m_windows.begin(), m_windows.end(),
          [&alive](SwitcherWindowId id) { return std::find(alive.begin(), alive.end(), id) == alive.end(); }
      ),
      m_windows.end()
  );
}

std::optional<size_t> SwitcherMru::appRank(SwitcherPid pid) const
{
  return rankOf(m_apps, pid);
}

std::optional<size_t> SwitcherMru::windowRank(SwitcherWindowId windowId) const
{
  if (windowId == 0) {
    return std::nullopt;
  }
  return rankOf(m_windows, windowId);
}

std::vector<size_t> orderSwitcherCandidates(
    const std::vector<SwitcherCandidate> &candidates, const std::vector<SwitcherWindowId> &zOrder,
    const SwitcherMru &mru
)
{
  constexpr size_t kUnknown = std::numeric_limits<size_t>::max();
  using Key = std::tuple<size_t, size_t, size_t, int, size_t>;

  std::vector<std::pair<Key, size_t>> keyed;
  keyed.reserve(candidates.size());
  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto &c = candidates[i];
    size_t z = kUnknown;
    if (c.windowId != 0 && !c.minimized) {
      z = rankOf(zOrder, c.windowId).value_or(kUnknown);
    }
    const size_t windowRank = mru.windowRank(c.windowId).value_or(kUnknown);
    const size_t appRank = mru.appRank(c.pid).value_or(kUnknown);
    keyed.emplace_back(Key{z, windowRank, appRank, c.appWindowIndex, i}, i);
  }
  std::sort(keyed.begin(), keyed.end());

  std::vector<size_t> order;
  order.reserve(keyed.size());
  for (const auto &[key, index] : keyed) {
    order.push_back(index);
  }
  return order;
}

size_t switcherInitialSelection(size_t count, bool reverse)
{
  if (count < 2) {
    return 0;
  }
  return reverse ? count - 1 : 1;
}

size_t switcherStep(size_t current, size_t count, int delta)
{
  if (count == 0) {
    return 0;
  }
  const auto n = static_cast<long long>(count);
  long long next = (static_cast<long long>(current % count) + delta) % n;
  if (next < 0) {
    next += n;
  }
  return static_cast<size_t>(next);
}

} // namespace deskflow::osx

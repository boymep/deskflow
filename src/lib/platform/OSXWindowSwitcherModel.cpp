/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXWindowSwitcherModel.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
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

  // on screen: by stacking order; off screen: by window MRU, app MRU, index in the app
  std::vector<std::pair<Key, size_t>> onScreen;
  std::vector<std::pair<Key, size_t>> offScreen;
  std::vector<std::pair<Key, size_t>> otherSpace;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto &c = candidates[i];
    size_t z = kUnknown;
    if (c.windowId != 0 && !c.minimized && !c.otherSpace) {
      z = rankOf(zOrder, c.windowId).value_or(kUnknown);
    }
    const size_t windowRank = mru.windowRank(c.windowId).value_or(kUnknown);
    const size_t appRank = mru.appRank(c.pid).value_or(kUnknown);
    const Key key{z, windowRank, appRank, c.appWindowIndex, i};
    if (z != kUnknown) {
      onScreen.emplace_back(key, i);
    } else if (c.otherSpace) {
      otherSpace.emplace_back(key, i);
    } else {
      offScreen.emplace_back(key, i);
    }
  }
  std::sort(onScreen.begin(), onScreen.end());
  std::sort(otherSpace.begin(), otherSpace.end());

  // a window in front was used after every window behind it: its effective
  // recency is the best window MRU rank from it to the back
  std::vector<size_t> recency(onScreen.size() + 1, kUnknown);
  for (size_t i = onScreen.size(); i-- > 0;) {
    recency[i] = std::min(recency[i + 1], std::get<1>(onScreen[i].first));
  }

  std::vector<size_t> order;
  order.reserve(candidates.size());
  size_t s = 0;
  size_t o = 0;
  if (!onScreen.empty()) {
    order.push_back(onScreen[s++].second); // the window in use stays first
  }
  while (s < onScreen.size()) {
    if (o < otherSpace.size() && std::get<1>(otherSpace[o].first) < recency[s]) {
      order.push_back(otherSpace[o++].second);
    } else {
      order.push_back(onScreen[s++].second);
    }
  }

  // the rest (other Spaces without a more recent use, minimized, hidden) by MRU
  std::vector<std::pair<Key, size_t>> rest(otherSpace.begin() + static_cast<std::ptrdiff_t>(o), otherSpace.end());
  rest.insert(rest.end(), offScreen.begin(), offScreen.end());
  std::sort(rest.begin(), rest.end());
  for (const auto &[key, index] : rest) {
    order.push_back(index);
  }
  return order;
}

bool switcherSpaceWindowPlausible(const SwitcherSpaceWindow &window, const std::vector<SwitcherPid> &allowed)
{
  return window.windowId != 0 && window.layer == 0 && std::isfinite(window.alpha) && window.alpha > 0 &&
         std::isfinite(window.frame.width) && std::isfinite(window.frame.height) &&
         window.frame.width >= kSwitcherMinSpaceWindowSize && window.frame.height >= kSwitcherMinSpaceWindowSize &&
         window.frame.width <= window.frame.height * kSwitcherMaxSpaceWindowAspect &&
         window.frame.height <= window.frame.width * kSwitcherMaxSpaceWindowAspect &&
         std::find(allowed.begin(), allowed.end(), window.pid) != allowed.end();
}

std::vector<size_t> selectOtherSpaceWindows(
    const std::vector<SwitcherSpaceWindow> &windows, const std::vector<SwitcherPid> &allowed,
    const std::vector<SwitcherWindowId> &known, const std::vector<uint64_t> &userSpaces,
    const std::vector<uint64_t> &visibleSpaces, size_t limit
)
{
  const auto contains = [](const auto &list, const auto &value) {
    return std::find(list.begin(), list.end(), value) != list.end();
  };
  std::vector<size_t> selected;
  std::vector<SwitcherWindowId> taken;
  for (size_t i = 0; i < windows.size() && selected.size() < limit; ++i) {
    const auto &w = windows[i];
    if (!switcherSpaceWindowPlausible(w, allowed) || w.spaces.empty() || contains(known, w.windowId) ||
        contains(taken, w.windowId)) {
      continue;
    }
    bool keep = true;
    for (const auto space : w.spaces) {
      if (!contains(userSpaces, space) || contains(visibleSpaces, space)) {
        keep = false;
        break;
      }
    }
    if (keep) {
      selected.push_back(i);
      taken.push_back(w.windowId);
    }
  }
  return selected;
}

std::array<uint8_t, kSwitcherRemoteTokenSize> switcherRemoteToken(SwitcherPid pid, uint64_t elementId)
{
  std::array<uint8_t, kSwitcherRemoteTokenSize> token{};
  const int32_t pid32 = pid;
  const int32_t zero = 0;
  const int32_t magic = 0x636f636f; // "coco"
  std::memcpy(token.data(), &pid32, sizeof(pid32));
  std::memcpy(token.data() + 4, &zero, sizeof(zero));
  std::memcpy(token.data() + 8, &magic, sizeof(magic));
  std::memcpy(token.data() + 12, &elementId, sizeof(elementId));
  return token;
}

std::array<uint8_t, kSwitcherEventRecordSize> switcherMakeKeyWindowEvent(SwitcherWindowId windowId, bool press)
{
  std::array<uint8_t, kSwitcherEventRecordSize> record{};
  record[0x04] = 0xf8;
  record[0x08] = press ? 0x01 : 0x02;
  record[0x3a] = 0x10;
  std::memset(record.data() + 0x20, 0xff, 0x10);
  const uint32_t id = windowId;
  std::memcpy(record.data() + 0x3c, &id, sizeof(id));
  return record;
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

SwitcherPixelSize switcherThumbnailSize(SwitcherRect window, SwitcherRect box, double scale, int maxPixelWidth)
{
  const auto valid = [](double v) { return std::isfinite(v) && v > 0; };
  if (!valid(window.width) || !valid(window.height) || !valid(box.width) || !valid(box.height) || !valid(scale) ||
      maxPixelWidth <= 0) {
    return {};
  }
  // aspect-fit into the box, but never upscale the window
  double factor = std::min({box.width / window.width, box.height / window.height, 1.0}) * scale;
  factor = std::min(factor, static_cast<double>(maxPixelWidth) / window.width);
  const auto px = [](double v) { return std::max(1, static_cast<int>(std::lround(v))); };
  return {std::min(px(window.width * factor), maxPixelWidth), px(window.height * factor)};
}

std::optional<size_t> matchSwitcherWindow(
    const SwitcherCaptureWindow &item, const std::vector<SwitcherCaptureWindow> &windows,
    const std::vector<SwitcherWindowId> &claimed
)
{
  if (item.windowId != 0) {
    for (size_t i = 0; i < windows.size(); ++i) {
      if (windows[i].windowId == item.windowId) {
        return i;
      }
    }
    return std::nullopt;
  }

  constexpr double kTolerance = 2.0;
  const bool hasFrame = item.frame.width > 0 && item.frame.height > 0;
  const auto frameClose = [&](const SwitcherRect &r) {
    return hasFrame && std::abs(r.x - item.frame.x) <= kTolerance && std::abs(r.y - item.frame.y) <= kTolerance &&
           std::abs(r.width - item.frame.width) <= kTolerance && std::abs(r.height - item.frame.height) <= kTolerance;
  };

  std::optional<size_t> titleMatch;
  std::optional<size_t> frameMatch;
  size_t titleMatches = 0;
  size_t frameMatches = 0;
  for (size_t i = 0; i < windows.size(); ++i) {
    const auto &w = windows[i];
    if (w.pid != item.pid || w.windowId == 0 ||
        std::find(claimed.begin(), claimed.end(), w.windowId) != claimed.end()) {
      continue;
    }
    const bool sameTitle = !item.title.empty() && w.title == item.title;
    const bool sameFrame = frameClose(w.frame);
    if (sameTitle && sameFrame) {
      return i;
    }
    if (sameTitle && titleMatches++ == 0) {
      titleMatch = i;
    }
    if (sameFrame && frameMatches++ == 0) {
      frameMatch = i;
    }
  }
  if (titleMatches == 1) {
    return titleMatch;
  }
  if (titleMatches == 0 && frameMatches == 1) {
    return frameMatch;
  }
  return std::nullopt;
}

std::vector<size_t> switcherCaptureOrder(size_t count, size_t selection, size_t limit)
{
  std::vector<size_t> order;
  if (count == 0 || limit == 0) {
    return order;
  }
  selection = std::min(selection, count - 1);
  order.push_back(selection);
  for (size_t i = 0; i < count && order.size() < limit; ++i) {
    if (i != selection) {
      order.push_back(i);
    }
  }
  return order;
}

std::vector<SwitcherDesktop> switcherEmptyDesktops(
    const std::vector<SwitcherSpaceEntry> &spaces, uint64_t current, const std::vector<uint64_t> &occupied
)
{
  std::vector<SwitcherDesktop> result;
  const auto currentIt = std::find_if(spaces.begin(), spaces.end(), [&](const auto &s) { return s.id == current; });
  if (currentIt == spaces.end()) {
    return result;
  }
  const auto currentPos = currentIt - spaces.begin();
  int number = 0;
  for (auto it = spaces.begin(); it != spaces.end(); ++it) {
    if (!it->desktop) {
      continue;
    }
    ++number;
    if (it->id == current || std::find(occupied.begin(), occupied.end(), it->id) != occupied.end()) {
      continue;
    }
    result.push_back(SwitcherDesktop{it->id, number, static_cast<int>((it - spaces.begin()) - currentPos)});
  }
  return result;
}

std::string switcherDesktopTitle(int number)
{
  return QCoreApplication::translate("OSXWindowSwitcher", "Desktop %1").arg(number).toStdString();
}

} // namespace deskflow::osx

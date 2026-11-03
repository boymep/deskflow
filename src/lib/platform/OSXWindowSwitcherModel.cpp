/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXWindowSwitcherModel.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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

const char *switcherDirectMethodName(SwitcherDirectMethod method)
{
  switch (method) {
  case SwitcherDirectMethod::Accessibility:
    return "A";
  case SwitcherDirectMethod::Activation:
    return "B";
  case SwitcherDirectMethod::WindowServer:
    return "C";
  }
  return "?";
}

std::vector<SwitcherDirectMethod> switcherDefaultDirectMethods()
{
  return {SwitcherDirectMethod::Accessibility, SwitcherDirectMethod::Activation, SwitcherDirectMethod::WindowServer};
}

std::vector<SwitcherDirectMethod> switcherParseDirectMethods(const std::string &spec)
{
  std::string lower;
  for (const char c : spec) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "-" || lower == "none") {
    return {};
  }
  std::vector<SwitcherDirectMethod> order;
  for (const char c : lower) {
    std::optional<SwitcherDirectMethod> method;
    if (c == 'a') {
      method = SwitcherDirectMethod::Accessibility;
    } else if (c == 'b') {
      method = SwitcherDirectMethod::Activation;
    } else if (c == 'c') {
      method = SwitcherDirectMethod::WindowServer;
    }
    if (method && std::find(order.begin(), order.end(), *method) == order.end()) {
      order.push_back(*method);
    }
  }
  return order.empty() ? switcherDefaultDirectMethods() : order;
}

std::vector<SwitcherDirectMethod>
switcherPromoteDirectMethod(const std::vector<SwitcherDirectMethod> &order, SwitcherDirectMethod method)
{
  std::vector<SwitcherDirectMethod> result{method};
  for (const auto m : order) {
    if (m != method) {
      result.push_back(m);
    }
  }
  return result;
}

SwitcherDirectPlan switcherPlanDirectSwitch(
    const std::vector<SwitcherDirectMethod> &order, size_t tried, uint64_t from, uint64_t target,
    const SwitcherDirectObservation &now, const SwitcherDirectTiming &timing
)
{
  using Action = SwitcherDirectPlan::Action;
  if (now.current == 0 || target == 0) {
    return {Action::FallBack};
  }
  if (now.current == target) {
    return {Action::Arrived};
  }
  if (tried > 0) {
    // a transition has started (or just ended and the current Space is not updated yet): wait for it
    const bool transition =
        now.animating || (now.sinceAnimation >= 0 && now.sinceAnimation < timing.settle);
    if (transition) {
      return {now.sinceMethod < timing.animationLimit ? Action::Wait : Action::FallBack};
    }
    if (now.current != from) {
      // landed elsewhere (another window of the app?): the exact window once more, else step from there
      return {now.canRetarget && !now.retargeted ? Action::Retarget : Action::FallBack};
    }
    if (now.sinceMethod < timing.perMethod) {
      return {Action::Wait};
    }
    if (now.retargeted) {
      return {Action::FallBack}; // the exact window did not move the display either
    }
  }
  if (tried >= order.size() || (tried > 0 && now.sinceStart >= timing.total)) {
    return {Action::FallBack};
  }
  return {Action::Try, order[tried]};
}

int switcherDesktopHotkeyId(int number)
{
  if (number < 1 || number > kSwitcherDesktopHotkeyCount) {
    return 0;
  }
  return kSwitcherDesktopHotkeyFirst + number - 1;
}

std::optional<SwitcherHotkey> switcherParseSymbolicHotkey(bool enabled, const std::vector<int64_t> &parameters)
{
  // Shift, Control, Option, Command and fn: the same bits in CGEventFlags and NSEventModifierFlags
  constexpr int64_t kModifierMask = 0x20000 | 0x40000 | 0x80000 | 0x100000 | 0x800000;
  constexpr int64_t kChordMask = 0x40000 | 0x80000 | 0x100000; // Control, Option, Command
  constexpr int64_t kMaxKeyCode = 0x7f;
  if (!enabled || parameters.size() < 3) {
    return std::nullopt;
  }
  const int64_t keyCode = parameters[1];
  const int64_t modifiers = parameters[2];
  if (keyCode < 0 || keyCode > kMaxKeyCode || modifiers <= 0 || (modifiers & kChordMask) == 0) {
    return std::nullopt; // 65535 means "no key"
  }
  return SwitcherHotkey{static_cast<uint16_t>(keyCode), static_cast<uint64_t>(modifiers & kModifierMask)};
}

int switcherDesktopNumber(const std::vector<SwitcherSpaceEntry> &entries, uint64_t spaceId)
{
  int number = 0;
  for (const auto &entry : entries) {
    if (entry.kind != SwitcherSpaceKind::Desktop) {
      continue;
    }
    ++number;
    if (entry.id == spaceId) {
      return number;
    }
  }
  return 0;
}

SwitcherHotkeyPlan
switcherPlanHotkeySwitch(uint64_t from, uint64_t target, const SwitcherHotkeyObservation &now, const SwitcherHotkeyTiming &timing)
{
  if (now.current != 0 && now.current == target) {
    return SwitcherHotkeyPlan::Arrived;
  }
  if (now.current != 0 && now.current != from) {
    return SwitcherHotkeyPlan::Elsewhere;
  }
  if (now.sinceStart >= timing.animationLimit) {
    return SwitcherHotkeyPlan::NoEffect;
  }
  if (now.animating || now.sinceStart < timing.timeout) {
    return SwitcherHotkeyPlan::Wait;
  }
  return SwitcherHotkeyPlan::NoEffect;
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

std::vector<SwitcherStripItem> switcherSpaceStrip(const std::vector<SwitcherSpaceEntry> &spaces, uint64_t current)
{
  std::vector<SwitcherStripItem> strip;
  int desktops = 0;
  for (const auto &space : spaces) {
    if (space.kind == SwitcherSpaceKind::Other || space.id == 0) {
      continue;
    }
    SwitcherStripItem item;
    item.spaceId = space.id;
    item.kind = space.kind;
    item.current = space.id == current;
    if (space.kind == SwitcherSpaceKind::Desktop) {
      item.number = ++desktops;
    } else {
      item.tiles = space.tiles;
    }
    strip.push_back(std::move(item));
  }
  const auto currentIt = std::find_if(strip.begin(), strip.end(), [](const auto &item) { return item.current; });
  if (currentIt == strip.end()) {
    return {};
  }
  const auto currentPos = static_cast<int>(currentIt - strip.begin());
  for (size_t i = 0; i < strip.size(); ++i) {
    strip[i].steps = static_cast<int>(i) - currentPos;
    if (desktops == 1) {
      strip[i].number = 0; // a single desktop has no number in Mission Control
    }
  }
  return strip;
}

void switcherTouchSpace(std::vector<uint64_t> &history, uint64_t id)
{
  if (id == 0) {
    return;
  }
  std::erase(history, id);
  history.insert(history.begin(), id);
}

void switcherPruneSpaceHistory(std::vector<uint64_t> &history, const std::vector<uint64_t> &alive)
{
  std::erase_if(history, [&alive](uint64_t id) { return std::find(alive.begin(), alive.end(), id) == alive.end(); });
}

std::vector<SwitcherStripItem>
switcherOrderByRecentUse(const std::vector<SwitcherStripItem> &strip, const std::vector<uint64_t> &history)
{
  std::vector<SwitcherStripItem> ordered;
  ordered.reserve(strip.size());
  std::vector<bool> taken(strip.size(), false);
  const auto take = [&](auto matches) {
    for (size_t i = 0; i < strip.size(); ++i) {
      if (!taken[i] && matches(strip[i])) {
        taken[i] = true;
        ordered.push_back(strip[i]);
      }
    }
  };
  take([](const SwitcherStripItem &item) { return item.current; });
  for (const auto id : history) {
    take([id](const SwitcherStripItem &item) { return item.spaceId == id; });
  }
  take([](const SwitcherStripItem &) { return true; });
  return ordered;
}

SwitcherSpaceStepPlan
switcherPlanSpaceStep(const std::vector<uint64_t> &order, uint64_t current, uint64_t target, uint64_t previousExpected)
{
  SwitcherSpaceStepPlan plan;
  if (previousExpected != 0 && current != previousExpected) {
    return plan; // not where the last step should have landed: someone else switched
  }
  const auto from = std::find(order.begin(), order.end(), current);
  const auto to = std::find(order.begin(), order.end(), target);
  if (current == 0 || target == 0 || from == order.end() || to == order.end()) {
    return plan;
  }
  const auto distance = static_cast<int>(to - from);
  if (distance == 0) {
    plan.action = SwitcherSpaceStepPlan::Action::Done;
    return plan;
  }
  plan.action = SwitcherSpaceStepPlan::Action::Step;
  plan.direction = distance > 0 ? 1 : -1;
  plan.remaining = std::abs(distance);
  plan.expected = *(from + plan.direction);
  return plan;
}

SwitcherSpacePosition
switcherSpaceStepPosition(const std::vector<uint64_t> &order, uint64_t current, uint64_t confirmed, uint64_t assumed)
{
  using Status = SwitcherSpacePosition::Status;
  if (current == 0) {
    return {};
  }
  if (assumed == 0 || current == assumed) {
    return {Status::Confirmed, current};
  }
  const auto from = std::find(order.begin(), order.end(), confirmed);
  const auto to = std::find(order.begin(), order.end(), assumed);
  const auto at = std::find(order.begin(), order.end(), current);
  if (from == order.end() || to == order.end() || at == order.end()) {
    return {};
  }
  // on the way from the confirmed Space to the assumed one (the current Space is reported late)
  if ((from <= at && at < to) || (to < at && at <= from)) {
    return {Status::Lagging, assumed};
  }
  return {};
}

SwitcherStepGate switcherStepGate(const SwitcherStepObservation &now, const SwitcherStepTiming &timing)
{
  if (now.sincePost >= timing.limit) {
    return now.landed ? SwitcherStepGate::Ready : SwitcherStepGate::Resync;
  }
  if (now.animating || now.sincePost < timing.minGap) {
    return SwitcherStepGate::Wait;
  }
  if (now.landed || now.sawAnimation) {
    return SwitcherStepGate::Ready; // the transition is over; the current Space may be reported later
  }
  return now.sincePost >= timing.trust ? SwitcherStepGate::Resync : SwitcherStepGate::Wait;
}

bool switcherSpacesSettled(const SwitcherSettleObservation &now, const SwitcherStepTiming &timing)
{
  if (now.sinceStart >= timing.limit) {
    return true; // waited long enough: a stuck transition flag must not block every switch
  }
  if (now.animating) {
    return false;
  }
  if (now.sinceAction < 0) {
    return true;
  }
  if (now.sinceAction < timing.settle) {
    return false;
  }
  return now.pendingLanded || now.sinceAction >= timing.trust;
}

std::vector<std::string> switcherFullscreenTitles(const std::vector<std::vector<SwitcherTileName>> &spaces)
{
  // apps with windows in several fullscreen Spaces are told apart by their window titles
  std::vector<std::pair<SwitcherPid, int>> spacesPerApp;
  for (const auto &tiles : spaces) {
    std::vector<SwitcherPid> seen;
    for (const auto &tile : tiles) {
      if (std::find(seen.begin(), seen.end(), tile.pid) != seen.end()) {
        continue;
      }
      seen.push_back(tile.pid);
      const auto it = std::find_if(spacesPerApp.begin(), spacesPerApp.end(), [&](const auto &e) {
        return e.first == tile.pid;
      });
      if (it == spacesPerApp.end()) {
        spacesPerApp.emplace_back(tile.pid, 1);
      } else {
        ++it->second;
      }
    }
  }
  std::vector<std::string> titles;
  titles.reserve(spaces.size());
  for (const auto &tiles : spaces) {
    std::string title;
    for (const auto &tile : tiles) {
      const auto it = std::find_if(spacesPerApp.begin(), spacesPerApp.end(), [&](const auto &e) {
        return e.first == tile.pid;
      });
      const bool shared = it != spacesPerApp.end() && it->second > 1;
      const std::string &name = shared && !tile.windowTitle.empty() ? tile.windowTitle : tile.appName;
      if (!title.empty()) {
        title += " & ";
      }
      title += name;
    }
    titles.push_back(std::move(title));
  }
  return titles;
}

std::string switcherDesktopTitle(int number)
{
  if (number <= 0) {
    return QCoreApplication::translate("OSXWindowSwitcher", "Desktop").toStdString();
  }
  return QCoreApplication::translate("OSXWindowSwitcher", "Desktop %1").arg(number).toStdString();
}

size_t switcherInitialSelection(const std::vector<SwitcherSelectable> &items, bool reverse)
{
  if (items.empty()) {
    return 0;
  }
  if (reverse) {
    return items.size() - 1;
  }
  std::optional<size_t> best;
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].current && items[i].rank.has_value() && (!best || *items[i].rank < *items[*best].rank)) {
      best = i;
    }
  }
  if (best) {
    return *best;
  }
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].window && !items[i].current) {
      return i;
    }
  }
  // only Spaces (and maybe the current window): the one after the current Space
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].window && items[i].current) {
      for (size_t k = 1; k < items.size(); ++k) {
        const size_t next = (i + k) % items.size();
        if (!items[next].current) {
          return next;
        }
      }
    }
  }
  return 0;
}

} // namespace deskflow::osx

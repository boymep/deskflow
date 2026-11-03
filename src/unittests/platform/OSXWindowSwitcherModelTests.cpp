/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXWindowSwitcherModelTests.h"

#include "platform/OSXWindowSwitcherModel.h"

using namespace deskflow::osx;

namespace {

std::vector<size_t>
order(const std::vector<SwitcherCandidate> &c, const std::vector<SwitcherWindowId> &z, const SwitcherMru &mru)
{
  return orderSwitcherCandidates(c, z, mru);
}

} // namespace

void OSXWindowSwitcherModelTests::mruTouchOrder()
{
  SwitcherMru mru;
  mru.touchWindow(10, 100);
  mru.touchWindow(20, 200);
  mru.touchWindow(10, 101);
  QCOMPARE(mru.appRank(10), std::optional<size_t>(0));
  QCOMPARE(mru.appRank(20), std::optional<size_t>(1));
  QCOMPARE(mru.windowRank(101), std::optional<size_t>(0));
  QCOMPARE(mru.windowRank(200), std::optional<size_t>(1));
  QCOMPARE(mru.windowRank(100), std::optional<size_t>(2));
  QVERIFY(!mru.windowRank(0).has_value());
  QVERIFY(!mru.appRank(30).has_value());
  // unknown window id only touches the app
  mru.touchWindow(20, 0);
  QCOMPARE(mru.appRank(20), std::optional<size_t>(0));
  QCOMPARE(mru.windowRank(101), std::optional<size_t>(0));
}

void OSXWindowSwitcherModelTests::mruBounded()
{
  SwitcherMru mru;
  for (uint32_t i = 1; i <= SwitcherMru::kMaxEntries + 50; ++i) {
    mru.touchWindow(static_cast<SwitcherPid>(i), i);
  }
  QVERIFY(!mru.windowRank(1).has_value());
  QVERIFY(!mru.appRank(1).has_value());
  QCOMPARE(mru.windowRank(SwitcherMru::kMaxEntries + 50), std::optional<size_t>(0));
  QCOMPARE(mru.windowRank(51), std::optional<size_t>(SwitcherMru::kMaxEntries - 1));
}

void OSXWindowSwitcherModelTests::mruRemoveAndRetain()
{
  SwitcherMru mru;
  mru.touchWindow(10, 100);
  mru.touchWindow(20, 200);
  mru.touchWindow(30, 300);
  mru.removeApp(20);
  QVERIFY(!mru.appRank(20).has_value());
  QCOMPARE(mru.appRank(10), std::optional<size_t>(1));
  mru.retainWindows({100});
  QVERIFY(!mru.windowRank(300).has_value());
  QCOMPARE(mru.windowRank(100), std::optional<size_t>(0));
}

void OSXWindowSwitcherModelTests::orderOnScreenFirst()
{
  SwitcherMru mru;
  mru.touchWindow(1, 11); // stale MRU must not beat the real stacking order
  const std::vector<SwitcherCandidate> c = {{1, 11, 0, false}, {2, 21, 0, false}, {3, 31, 0, false}};
  QCOMPARE(order(c, {31, 11, 21}, mru), (std::vector<size_t>{2, 0, 1}));
}

void OSXWindowSwitcherModelTests::orderOffScreenByMru()
{
  SwitcherMru mru;
  mru.touchWindow(3, 31); // hidden app used last
  mru.touchWindow(2, 21); // minimized window used most recently
  // on screen: 11; off screen: 21 (minimized), 31 (hidden app), 41 (never seen)
  const std::vector<SwitcherCandidate> c = {
      {4, 41, 0, false}, {3, 31, 0, false}, {2, 21, 0, true}, {1, 11, 0, false}
  };
  QCOMPARE(order(c, {11}, mru), (std::vector<size_t>{3, 2, 1, 0}));
}

void OSXWindowSwitcherModelTests::orderFallbackAppAndIndex()
{
  SwitcherMru mru;
  mru.touchApp(2);
  mru.touchApp(1); // app 1 most recent
  // no window ids (CGWindowID lookup unavailable): app MRU, then the app's window order
  const std::vector<SwitcherCandidate> c = {{2, 0, 0, false}, {1, 0, 1, false}, {1, 0, 0, false}, {5, 0, 0, false}};
  QCOMPARE(order(c, {}, mru), (std::vector<size_t>{2, 1, 0, 3}));
}

void OSXWindowSwitcherModelTests::orderMinimizedIgnoresStaleZ()
{
  SwitcherMru mru;
  // a minimized window id still in a z snapshot is treated as off screen
  const std::vector<SwitcherCandidate> c = {{1, 11, 0, true}, {2, 21, 0, false}};
  QCOMPARE(order(c, {11, 21}, mru), (std::vector<size_t>{1, 0}));
}

void OSXWindowSwitcherModelTests::initialSelection()
{
  QCOMPARE(switcherInitialSelection(0, false), size_t(0));
  QCOMPARE(switcherInitialSelection(1, false), size_t(0));
  QCOMPARE(switcherInitialSelection(5, false), size_t(1));
  QCOMPARE(switcherInitialSelection(5, true), size_t(4));
  QCOMPARE(switcherInitialSelection(1, true), size_t(0));
}

void OSXWindowSwitcherModelTests::stepWraps()
{
  QCOMPARE(switcherStep(4, 5, 1), size_t(0));
  QCOMPARE(switcherStep(0, 5, -1), size_t(4));
  QCOMPARE(switcherStep(2, 5, 1), size_t(3));
  QCOMPARE(switcherStep(0, 0, 1), size_t(0));
  QCOMPARE(switcherStep(1, 3, -7), size_t(0));
}

QTEST_MAIN(OSXWindowSwitcherModelTests)

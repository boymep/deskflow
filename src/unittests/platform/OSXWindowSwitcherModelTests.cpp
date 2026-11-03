/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXWindowSwitcherModelTests.h"

#include "platform/OSXWindowSwitcherModel.h"

#include <limits>

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

void OSXWindowSwitcherModelTests::orderOtherSpacesMixedByMru()
{
  SwitcherMru mru;
  mru.touchWindow(5, 51); // other Space, used long ago
  mru.touchWindow(3, 31); // on screen, behind
  mru.touchWindow(4, 41); // other Space, used after 31
  mru.touchWindow(1, 11); // frontmost
  // on screen front to back: 11, 21 (never tracked), 31; other Spaces: 41, 51, 61 (never seen); minimized: 71
  const std::vector<SwitcherCandidate> c = {{1, 11, 0, false},       {2, 21, 0, false},       {3, 31, 0, false},
                                            {4, 41, 0, false, true}, {5, 51, 0, false, true}, {6, 61, 0, false, true},
                                            {7, 71, 0, true}};
  // 41 was used after 21 and 31 (21 is in front of 31, so used after it, but 41 beat both)
  QCOMPARE(order(c, {11, 21, 31, 41}, mru), (std::vector<size_t>{0, 3, 1, 2, 4, 5, 6}));

  // the frontmost window stays first even if a window on another Space ranks better
  SwitcherMru stale;
  stale.touchWindow(4, 41);
  QCOMPARE(order({{1, 11, 0, false}, {4, 41, 0, false, true}}, {11}, stale), (std::vector<size_t>{0, 1}));
}

void OSXWindowSwitcherModelTests::orderOtherSpacesWithoutOnScreen()
{
  SwitcherMru mru;
  mru.touchWindow(2, 21);
  mru.touchWindow(4, 41);
  // empty current Space: other Spaces and minimized windows by MRU
  const std::vector<SwitcherCandidate> c = {{2, 21, 0, true}, {3, 31, 0, false, true}, {4, 41, 0, false, true}};
  QCOMPARE(order(c, {}, mru), (std::vector<size_t>{2, 0, 1}));
}

void OSXWindowSwitcherModelTests::spaceWindowPlausible()
{
  const std::vector<SwitcherPid> allowed = {7};
  SwitcherSpaceWindow w{1, 7, 0, 1.0, {0, 0, 400, 300}, {}};
  QVERIFY(switcherSpaceWindowPlausible(w, allowed));
  auto bad = w;
  bad.windowId = 0;
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.layer = 3;
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.alpha = 0;
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.frame.height = 20;
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.frame = {0, 0, 1728, 158}; // toolbar strip of a fullscreen browser
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.frame = {0, 0, 120, 900};
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.frame.width = std::numeric_limits<double>::quiet_NaN();
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
  bad = w;
  bad.pid = 8;
  QVERIFY(!switcherSpaceWindowPlausible(bad, allowed));
}

void OSXWindowSwitcherModelTests::selectOtherSpaces()
{
  const SwitcherRect frame{0, 0, 800, 600};
  const std::vector<SwitcherSpaceWindow> w = {
      {1, 7, 0, 1.0, frame, {2}},    // other desktop: kept
      {2, 7, 0, 1.0, frame, {1}},    // visible Space, not reported by Accessibility: dropped
      {3, 7, 0, 1.0, frame, {4}},    // fullscreen Space: kept
      {4, 7, 0, 1.0, frame, {}},     // no Space: dropped
      {5, 7, 0, 1.0, frame, {99}},   // system Space: dropped
      {6, 7, 0, 1.0, frame, {2}},    // known via Accessibility: dropped
      {1, 7, 0, 1.0, frame, {2}},    // duplicate: dropped
      {7, 7, 0, 1.0, frame, {2, 1}}, // also on the visible Space: dropped
      {8, 9, 0, 1.0, frame, {2}},    // process not allowed: dropped
      {9, 7, 0, 1.0, frame, {2, 4}}, // kept
  };
  const std::vector<uint64_t> user = {1, 2, 4};
  QCOMPARE(selectOtherSpaceWindows(w, {7}, {6}, user, {1}, 10), (std::vector<size_t>{0, 2, 9}));
  QCOMPARE(selectOtherSpaceWindows(w, {7}, {6}, user, {1}, 2), (std::vector<size_t>{0, 2}));
  QVERIFY(selectOtherSpaceWindows(w, {7}, {6}, user, {1}, 0).empty());
}

void OSXWindowSwitcherModelTests::remoteToken()
{
  const auto token = switcherRemoteToken(0x01020304, 0x1122334455667788ULL);
  const std::array<uint8_t, kSwitcherRemoteTokenSize> expected = {0x04, 0x03, 0x02, 0x01, 0,    0,    0,
                                                                  0,    0x6f, 0x63, 0x6f, 0x63, 0x88, 0x77,
                                                                  0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
  QVERIFY(token == expected);
}

void OSXWindowSwitcherModelTests::makeKeyWindowEvent()
{
  const auto press = switcherMakeKeyWindowEvent(0xa1b2c3d4, true);
  const auto release = switcherMakeKeyWindowEvent(0xa1b2c3d4, false);
  QCOMPARE(press.size(), size_t(0xf8));
  QCOMPARE(int(press[0x04]), 0xf8);
  QCOMPARE(int(press[0x08]), 0x01);
  QCOMPARE(int(release[0x08]), 0x02);
  QCOMPARE(int(press[0x3a]), 0x10);
  for (size_t i = 0x20; i < 0x30; ++i) {
    QCOMPARE(int(press[i]), 0xff);
  }
  QCOMPARE(int(press[0x3c]), 0xd4);
  QCOMPARE(int(press[0x3f]), 0xa1);
  QCOMPARE(int(press[0x00]), 0);
  QCOMPARE(int(press[0x40]), 0);
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

void OSXWindowSwitcherModelTests::thumbnailSizeFits()
{
  const SwitcherRect box{0, 0, 200, 124};
  // wide window: limited by the box width at 2x
  QCOMPARE(switcherThumbnailSize({0, 0, 1600, 900}, box, 2.0, 400), (SwitcherPixelSize{400, 225}));
  // tall window: limited by the box height
  QCOMPARE(switcherThumbnailSize({0, 0, 600, 1200}, box, 2.0, 400), (SwitcherPixelSize{124, 248}));
  // 1x screen
  QCOMPARE(switcherThumbnailSize({0, 0, 1600, 900}, box, 1.0, 400), (SwitcherPixelSize{200, 113}));
  // max pixel width caps a high scale
  QCOMPARE(switcherThumbnailSize({0, 0, 1600, 900}, box, 3.0, 320), (SwitcherPixelSize{320, 180}));
  // small window is not upscaled beyond its own pixels
  QCOMPARE(switcherThumbnailSize({0, 0, 100, 50}, box, 2.0, 400), (SwitcherPixelSize{200, 100}));
  // extreme aspect still yields at least one pixel
  QCOMPARE(switcherThumbnailSize({0, 0, 4000, 1}, box, 1.0, 400), (SwitcherPixelSize{200, 1}));
}

void OSXWindowSwitcherModelTests::thumbnailSizeInvalid()
{
  const SwitcherRect box{0, 0, 200, 124};
  QCOMPARE(switcherThumbnailSize({0, 0, 0, 900}, box, 2.0, 400), SwitcherPixelSize{});
  QCOMPARE(switcherThumbnailSize({0, 0, 800, 600}, {0, 0, 0, 0}, 2.0, 400), SwitcherPixelSize{});
  QCOMPARE(switcherThumbnailSize({0, 0, 800, 600}, box, 0.0, 400), SwitcherPixelSize{});
  QCOMPARE(switcherThumbnailSize({0, 0, 800, 600}, box, 2.0, 0), SwitcherPixelSize{});
  QCOMPARE(
      switcherThumbnailSize({0, 0, std::numeric_limits<double>::infinity(), 600}, box, 2.0, 400), SwitcherPixelSize{}
  );
}

void OSXWindowSwitcherModelTests::matchById()
{
  const std::vector<SwitcherCaptureWindow> windows{
      {100, 10, "Doc", {0, 0, 800, 600}}, {101, 10, "Doc", {0, 0, 800, 600}}
  };
  QCOMPARE(matchSwitcherWindow({101, 10, "", {}}, windows, {}), std::optional<size_t>(1));
  // a known id never falls back to guessing
  QVERIFY(!matchSwitcherWindow({555, 10, "Doc", {0, 0, 800, 600}}, windows, {}).has_value());
}

void OSXWindowSwitcherModelTests::matchByTitleAndFrame()
{
  const std::vector<SwitcherCaptureWindow> windows{
      {100, 10, "Inbox", {0, 25, 800, 600}},
      {101, 10, "Draft", {40, 60, 500, 400}},
      {102, 20, "Inbox", {0, 25, 800, 600}}, // other process
      {103, 10, "Notes", {300, 300, 640, 480}},
  };
  // title and frame (frame within 2 points)
  QCOMPARE(matchSwitcherWindow({0, 10, "Inbox", {1, 24, 801, 599}}, windows, {}), std::optional<size_t>(0));
  // unique title, frame changed (window moved since)
  QCOMPARE(matchSwitcherWindow({0, 10, "Draft", {0, 0, 10, 10}}, windows, {}), std::optional<size_t>(1));
  // no title, unique frame
  QCOMPARE(matchSwitcherWindow({0, 10, "", {300, 300, 640, 480}}, windows, {}), std::optional<size_t>(3));
  // other process only
  QCOMPARE(matchSwitcherWindow({0, 20, "Inbox", {}}, windows, {}), std::optional<size_t>(2));
  // claimed windows are skipped
  QVERIFY(!matchSwitcherWindow({0, 10, "Inbox", {0, 25, 800, 600}}, windows, {100}).has_value());
  // nothing similar
  QVERIFY(!matchSwitcherWindow({0, 10, "Other", {900, 900, 10, 10}}, windows, {}).has_value());
  QVERIFY(!matchSwitcherWindow({0, 30, "Inbox", {0, 25, 800, 600}}, windows, {}).has_value());
}

void OSXWindowSwitcherModelTests::matchAmbiguous()
{
  const std::vector<SwitcherCaptureWindow> windows{
      {100, 10, "Untitled", {0, 0, 800, 600}},
      {101, 10, "Untitled", {50, 50, 800, 600}},
      {102, 10, "", {50, 50, 800, 600}},
  };
  // same title twice: the frame decides
  QCOMPARE(matchSwitcherWindow({0, 10, "Untitled", {50, 50, 800, 600}}, windows, {}), std::optional<size_t>(1));
  // same title twice and no frame: ambiguous
  QVERIFY(!matchSwitcherWindow({0, 10, "Untitled", {}}, windows, {}).has_value());
  // no title and two windows with the same frame: ambiguous
  QVERIFY(!matchSwitcherWindow({0, 10, "", {50, 50, 800, 600}}, windows, {}).has_value());
  // ... unless one is already taken
  QCOMPARE(matchSwitcherWindow({0, 10, "", {50, 50, 800, 600}}, windows, {101}), std::optional<size_t>(2));
}

void OSXWindowSwitcherModelTests::captureOrder()
{
  QCOMPARE(switcherCaptureOrder(0, 0, 30), std::vector<size_t>{});
  QCOMPARE(switcherCaptureOrder(5, 0, 0), std::vector<size_t>{});
  QCOMPARE(switcherCaptureOrder(4, 1, 30), (std::vector<size_t>{1, 0, 2, 3}));
  QCOMPARE(switcherCaptureOrder(6, 0, 3), (std::vector<size_t>{0, 1, 2}));
  // reverse open selects the last item, which is captured first even past the limit
  QCOMPARE(switcherCaptureOrder(50, 49, 3), (std::vector<size_t>{49, 0, 1}));
  QCOMPARE(switcherCaptureOrder(3, 9, 30), (std::vector<size_t>{2, 0, 1}));
}

void OSXWindowSwitcherModelTests::lruBoundedAndRecent()
{
  SwitcherLru<SwitcherWindowId, int> lru(2);
  QVERIFY(lru.find(1) == nullptr);
  lru.put(1, 10);
  lru.put(2, 20);
  QCOMPARE(*lru.find(1), 10); // 1 becomes most recent
  lru.put(3, 30);             // evicts 2
  QVERIFY(lru.find(2) == nullptr);
  QCOMPARE(*lru.find(1), 10);
  QCOMPARE(*lru.find(3), 30);
  lru.put(3, 31); // replace
  QCOMPARE(lru.size(), size_t(2));
  QCOMPARE(*lru.find(3), 31);
  lru.erase(3);
  QCOMPARE(lru.size(), size_t(1));
  lru.clear();
  QCOMPARE(lru.size(), size_t(0));

  SwitcherLru<SwitcherWindowId, int> none(0);
  none.put(1, 1);
  QCOMPARE(none.size(), size_t(0));
}

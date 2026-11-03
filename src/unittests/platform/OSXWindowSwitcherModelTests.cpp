/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXWindowSwitcherModelTests.h"

#include "platform/OSXWindowSwitcherModel.h"

#include <limits>

using namespace deskflow::osx;

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

void OSXWindowSwitcherModelTests::spaceStrip()
{
  // like Mission Control: desktop 1, fullscreen app, desktop 2, a system Space, split view
  const std::vector<SwitcherSpaceEntry> spaces{
      {10, SwitcherSpaceKind::Desktop, {}},
      {11, SwitcherSpaceKind::Fullscreen, {{7, 100}}},
      {12, SwitcherSpaceKind::Desktop, {}},
      {13, SwitcherSpaceKind::Other, {}},
      {14, SwitcherSpaceKind::Fullscreen, {{7, 101}, {8, 102}}},
  };
  const auto strip = switcherSpaceStrip(spaces, 12);
  QCOMPARE(strip.size(), size_t(4));
  QCOMPARE(strip[0].spaceId, uint64_t(10));
  QCOMPARE(strip[0].number, 1);
  QCOMPARE(strip[0].steps, -2);
  QVERIFY(!strip[0].current);
  QCOMPARE(strip[1].kind, SwitcherSpaceKind::Fullscreen);
  QCOMPARE(strip[1].number, 0);
  QCOMPARE(strip[1].steps, -1);
  QCOMPARE(strip[1].tiles.size(), size_t(1));
  QCOMPARE(strip[1].tiles[0].windowId, SwitcherWindowId(100));
  QCOMPARE(strip[2].number, 2);
  QCOMPARE(strip[2].steps, 0);
  QVERIFY(strip[2].current);
  QCOMPARE(strip[3].spaceId, uint64_t(14)); // the system Space is neither shown nor a step
  QCOMPARE(strip[3].steps, 1);
  QCOMPARE(strip[3].tiles.size(), size_t(2));
}

void OSXWindowSwitcherModelTests::spaceStripSingleDesktop()
{
  // one desktop and three fullscreen Spaces, the current one fullscreen: the desktop has no number
  const std::vector<SwitcherSpaceEntry> spaces{
      {1, SwitcherSpaceKind::Desktop, {}},
      {1143, SwitcherSpaceKind::Fullscreen, {{5, 22992}}},
      {1155, SwitcherSpaceKind::Fullscreen, {{5, 21590}}},
      {1149, SwitcherSpaceKind::Fullscreen, {{6, 737}}},
  };
  const auto strip = switcherSpaceStrip(spaces, 1143);
  QCOMPARE(strip.size(), size_t(4));
  QCOMPARE(strip[0].number, 0);
  QCOMPARE(strip[0].steps, -1);
  QVERIFY(strip[1].current);
  QCOMPARE(strip[3].steps, 2);
}

void OSXWindowSwitcherModelTests::directMethodOrder()
{
  using M = SwitcherDirectMethod;
  const std::vector<M> defaults{M::Accessibility, M::Activation, M::WindowServer};
  QVERIFY(switcherDefaultDirectMethods() == defaults);
  QVERIFY(switcherParseDirectMethods("") == defaults);
  QVERIFY(switcherParseDirectMethods("xyz") == defaults);
  QVERIFY((switcherParseDirectMethods("C,a b") == std::vector<M>{M::WindowServer, M::Accessibility, M::Activation}));
  QVERIFY((switcherParseDirectMethods("cc") == std::vector<M>{M::WindowServer}));
  QVERIFY(switcherParseDirectMethods("none").empty());
  QVERIFY(switcherParseDirectMethods("-").empty());
  QVERIFY((switcherPromoteDirectMethod(defaults, M::WindowServer) ==
           std::vector<M>{M::WindowServer, M::Accessibility, M::Activation}));
  QVERIFY(switcherPromoteDirectMethod(defaults, M::Accessibility) == defaults);
  QCOMPARE(QString(switcherDirectMethodName(M::Accessibility)), QString("A"));
  QCOMPARE(QString(switcherDirectMethodName(M::Activation)), QString("B"));
  QCOMPARE(QString(switcherDirectMethodName(M::WindowServer)), QString("C"));
}

void OSXWindowSwitcherModelTests::directSwitchPlanner()
{
  using Action = SwitcherDirectPlan::Action;
  using M = SwitcherDirectMethod;
  const auto order = switcherDefaultDirectMethods();
  const SwitcherDirectTiming timing;
  auto at = [](uint64_t current, double sinceStart, double sinceMethod) {
    SwitcherDirectObservation o;
    o.current = current;
    o.sinceStart = sinceStart;
    o.sinceMethod = sinceMethod;
    return o;
  };

  // the first method right away
  auto plan = switcherPlanDirectSwitch(order, 0, 1, 9, at(1, 0, 0), timing);
  QCOMPARE(plan.action, Action::Try);
  QCOMPARE(plan.method, M::Accessibility);
  // nothing happened yet: wait, then the next method
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, at(1, 0.1, 0.1), timing).action, Action::Wait);
  plan = switcherPlanDirectSwitch(order, 1, 1, 9, at(1, 0.36, 0.36), timing);
  QCOMPARE(plan.action, Action::Try);
  QCOMPARE(plan.method, M::Activation);
  plan = switcherPlanDirectSwitch(order, 2, 1, 9, at(1, 0.72, 0.36), timing);
  QCOMPARE(plan.action, Action::Try);
  QCOMPARE(plan.method, M::WindowServer);
  // all failed: step
  QCOMPARE(switcherPlanDirectSwitch(order, 3, 1, 9, at(1, 1.08, 0.36), timing).action, Action::FallBack);
  // out of time before the last method: step
  QCOMPARE(switcherPlanDirectSwitch(order, 2, 1, 9, at(1, 1.1, 0.36), timing).action, Action::FallBack);
  // arrived
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, at(9, 0.2, 0.2), timing).action, Action::Arrived);
  // landed on another Space: step from there
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, at(5, 0.2, 0.2), timing).action, Action::FallBack);
  // no methods or unknown Spaces: step at once
  QCOMPARE(switcherPlanDirectSwitch({}, 0, 1, 9, at(1, 0, 0), timing).action, Action::FallBack);
  QCOMPARE(switcherPlanDirectSwitch(order, 0, 1, 9, at(0, 0, 0), timing).action, Action::FallBack);
  // a promoted order starts with the method that worked
  plan = switcherPlanDirectSwitch(switcherPromoteDirectMethod(order, M::WindowServer), 0, 1, 9, at(1, 0, 0), timing);
  QCOMPARE(plan.method, M::WindowServer);
}

void OSXWindowSwitcherModelTests::directSwitchPlannerWaitsForTransition()
{
  using Action = SwitcherDirectPlan::Action;
  const auto order = switcherDefaultDirectMethods();
  const SwitcherDirectTiming timing;
  SwitcherDirectObservation o;
  o.current = 1;
  o.sinceStart = 0.9;
  o.sinceMethod = 0.9;
  o.animating = true;
  // a transition is running: no new method and no stepping, even past the per-method wait
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::Wait);
  // it just ended, the current Space is not updated yet
  o.animating = false;
  o.sinceAnimation = 0.1;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::Wait);
  // ended long ago without arriving: the next method
  o.sinceAnimation = 0.5;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::Try);
  // a transition that never ends: step
  o.animating = true;
  o.sinceMethod = 2.5;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::FallBack);
}

void OSXWindowSwitcherModelTests::spaceStepPlanner()
{
  using Action = SwitcherSpaceStepPlan::Action;
  const std::vector<uint64_t> order{11, 22, 33, 44};

  // Desktop 1 -> Desktop 3: one step at a time, each recomputed from the Space that is current
  auto plan = switcherPlanSpaceStep(order, 11, 33, 0);
  QCOMPARE(plan.action, Action::Step);
  QCOMPARE(plan.direction, 1);
  QCOMPARE(plan.remaining, 2);
  QCOMPARE(plan.expected, uint64_t{22});
  plan = switcherPlanSpaceStep(order, 22, 33, plan.expected);
  QCOMPARE(plan.action, Action::Step);
  QCOMPARE(plan.remaining, 1);
  QCOMPARE(plan.expected, uint64_t{33});
  // arrived: never one step more
  QCOMPARE(switcherPlanSpaceStep(order, 33, 33, plan.expected).action, Action::Done);

  // to the left
  plan = switcherPlanSpaceStep(order, 44, 22, 0);
  QCOMPARE(plan.action, Action::Step);
  QCOMPARE(plan.direction, -1);
  QCOMPARE(plan.remaining, 2);
  QCOMPARE(plan.expected, uint64_t{33});

  // already there
  QCOMPARE(switcherPlanSpaceStep(order, 22, 22, 0).action, Action::Done);
}

void OSXWindowSwitcherModelTests::spaceStepPlannerAborts()
{
  using Action = SwitcherSpaceStepPlan::Action;
  const std::vector<uint64_t> order{11, 22, 33, 44};

  // a step that went one Space too far (e.g. a doubled key event) must not be corrected by stepping on
  QCOMPARE(switcherPlanSpaceStep(order, 33, 33, 22).action, Action::Abort);
  // the user switched elsewhere meanwhile
  QCOMPARE(switcherPlanSpaceStep(order, 11, 33, 22).action, Action::Abort);
  // unknown Spaces
  QCOMPARE(switcherPlanSpaceStep(order, 99, 33, 0).action, Action::Abort);
  QCOMPARE(switcherPlanSpaceStep(order, 11, 99, 0).action, Action::Abort);
  QCOMPARE(switcherPlanSpaceStep(order, 0, 33, 0).action, Action::Abort);
  QCOMPARE(switcherPlanSpaceStep({}, 11, 11, 0).action, Action::Abort);
}

void OSXWindowSwitcherModelTests::spaceStripUnknownCurrent()
{
  const std::vector<SwitcherSpaceEntry> spaces{{1, SwitcherSpaceKind::Desktop, {}}, {2, SwitcherSpaceKind::Other, {}}};
  QVERIFY(switcherSpaceStrip(spaces, 99).empty());
  QVERIFY(switcherSpaceStrip(spaces, 2).empty()); // a system Space has no place in the strip
  QVERIFY(switcherSpaceStrip({}, 1).empty());
}

void OSXWindowSwitcherModelTests::fullscreenTitles()
{
  // two Spaces of the same app go by window title, a single one by app name, split views join
  const std::vector<std::vector<SwitcherTileName>> spaces{
      {{5, "Code", "Keyboard switching"}},
      {{5, "Code", "ORBIT-220 — moex"}},
      {{6, "Google Chrome", "New Tab"}},
      {{7, "Notes", ""}, {8, "Safari", "Apple"}},
      {{9, "Mail", ""}},
      {{9, "Mail", ""}}, // title unknown: app name
  };
  QCOMPARE(
      switcherFullscreenTitles(spaces), (std::vector<std::string>{
                                            "Keyboard switching", "ORBIT-220 — moex", "Google Chrome",
                                            "Notes & Safari", "Mail", "Mail"
                                        })
  );
  QVERIFY(switcherFullscreenTitles({}).empty());
}

void OSXWindowSwitcherModelTests::initialSelectionMostRecent()
{
  using S = SwitcherSelectable;
  // strip: desktop, current fullscreen (rank 0), fullscreen (rank 2); windows: rank 1, rank 3
  std::vector<S> items{
      {std::nullopt, false, false}, {0, true, false}, {2, false, false}, {1, false, true}, {3, false, true}
  };
  QCOMPARE(switcherInitialSelection(items, false), size_t(3));
  QCOMPARE(switcherInitialSelection(items, true), size_t(4));
  // the previous fullscreen app was used more recently than any window
  items[2].rank = 1;
  items[3].rank = 2;
  QCOMPARE(switcherInitialSelection(items, false), size_t(2));
}

void OSXWindowSwitcherModelTests::initialSelectionFallbacks()
{
  using S = SwitcherSelectable;
  // no ranks: the first window that is not current (the previous window on a desktop)
  const std::vector<S> desktop{
      {std::nullopt, true, false}, {std::nullopt, false, false}, {std::nullopt, true, true}, {std::nullopt, false, true}
  };
  QCOMPARE(switcherInitialSelection(desktop, false), size_t(3));
  // only Spaces: the one after the current, wrapping
  const std::vector<S> strip{{std::nullopt, false, false}, {std::nullopt, false, false}, {std::nullopt, true, false}};
  QCOMPARE(switcherInitialSelection(strip, false), size_t(0));
  QCOMPARE(switcherInitialSelection(std::vector<S>{{std::nullopt, true, false}}, false), size_t(0));
  QCOMPARE(switcherInitialSelection(std::vector<S>{}, false), size_t(0));
}

void OSXWindowSwitcherModelTests::spaceRecentUseOrder()
{
  const auto item = [](uint64_t id, int steps, bool current) {
    SwitcherStripItem strip;
    strip.spaceId = id;
    strip.steps = steps;
    strip.current = current;
    return strip;
  };
  // Mission Control order 1 2 3 4 5, the current Space is 3
  const std::vector<SwitcherStripItem> strip{item(1, -2, false), item(2, -1, false), item(3, 0, true),
                                             item(4, 1, false),  item(5, 2, false)};
  const auto ids = [](const std::vector<SwitcherStripItem> &items) {
    std::vector<uint64_t> out;
    for (const auto &i : items) {
      out.push_back(i.spaceId);
    }
    return out;
  };
  using Ids = std::vector<uint64_t>;

  // no history: the current Space first, the rest in Mission Control order
  QCOMPARE(ids(switcherOrderByRecentUse(strip, {})), (Ids{3, 1, 2, 4, 5}));
  // visited Spaces by recency, never visited ones after them
  QCOMPARE(ids(switcherOrderByRecentUse(strip, {3, 5, 2})), (Ids{3, 5, 2, 1, 4}));
  // the current Space is first even if the history is stale
  QCOMPARE(ids(switcherOrderByRecentUse(strip, {5, 3, 2})), (Ids{3, 5, 2, 1, 4}));
  // unknown ids are ignored
  QCOMPARE(ids(switcherOrderByRecentUse(strip, {99, 4})), (Ids{3, 4, 1, 2, 5}));
  // switching still counts steps by the real order
  const auto ordered = switcherOrderByRecentUse(strip, {3, 5});
  QCOMPARE(ordered[1].spaceId, uint64_t(5));
  QCOMPARE(ordered[1].steps, 2);
  QCOMPARE(ordered[2].steps, -2);
  QVERIFY(switcherOrderByRecentUse({}, {1}).empty());

  Ids history;
  switcherTouchSpace(history, 2);
  switcherTouchSpace(history, 7);
  switcherTouchSpace(history, 2);
  switcherTouchSpace(history, 0);
  QCOMPARE(history, (Ids{2, 7}));
  switcherTouchSpace(history, 9);
  switcherPruneSpaceHistory(history, {9, 7});
  QCOMPARE(history, (Ids{9, 7}));
}

void OSXWindowSwitcherModelTests::desktopTitle()
{
  QCOMPARE(QString::fromStdString(switcherDesktopTitle(3)), QStringLiteral("Desktop 3"));
  QCOMPARE(QString::fromStdString(switcherDesktopTitle(0)), QStringLiteral("Desktop"));
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

void OSXWindowSwitcherModelTests::directSwitchPlannerRetargets()
{
  using Action = SwitcherDirectPlan::Action;
  const auto order = switcherDefaultDirectMethods();
  const SwitcherDirectTiming timing;
  SwitcherDirectObservation o;
  o.current = 5; // landed on another window's Space
  o.sinceStart = 0.25;
  o.sinceMethod = 0.25;
  o.canRetarget = true;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::Retarget);
  // without the element: step from there
  o.canRetarget = false;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 1, 9, o, timing).action, Action::FallBack);
  // retried from Space 5: wait, then arrive or step (never the next method)
  o.canRetarget = true;
  o.retargeted = true;
  o.sinceMethod = 0.1;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 5, 9, o, timing).action, Action::Wait);
  o.sinceMethod = 0.4;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 5, 9, o, timing).action, Action::FallBack);
  o.current = 9;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 5, 9, o, timing).action, Action::Arrived);
  // landed wrong once more: no second retry
  o.current = 7;
  o.sinceMethod = 0.1;
  QCOMPARE(switcherPlanDirectSwitch(order, 1, 5, 9, o, timing).action, Action::FallBack);
}

void OSXWindowSwitcherModelTests::spaceStepPosition()
{
  using Status = SwitcherSpacePosition::Status;
  const std::vector<uint64_t> order{1, 2, 3, 4, 5};
  // before the first step: the current Space
  auto p = switcherSpaceStepPosition(order, 2, 0, 0);
  QCOMPARE(p.status, Status::Confirmed);
  QCOMPARE(p.position, uint64_t(2));
  // the steps showed up
  p = switcherSpaceStepPosition(order, 4, 2, 4);
  QCOMPARE(p.status, Status::Confirmed);
  QCOMPARE(p.position, uint64_t(4));
  // reported late: still on the way, go on from where the steps lead
  for (const uint64_t current : {uint64_t(2), uint64_t(3)}) {
    p = switcherSpaceStepPosition(order, current, 2, 4);
    QCOMPARE(p.status, Status::Lagging);
    QCOMPARE(p.position, uint64_t(4));
  }
  p = switcherSpaceStepPosition(order, 4, 5, 3);
  QCOMPARE(p.status, Status::Lagging);
  // off the way: someone else switched
  QCOMPARE(switcherSpaceStepPosition(order, 1, 2, 4).status, Status::Elsewhere);
  QCOMPARE(switcherSpaceStepPosition(order, 5, 2, 4).status, Status::Elsewhere);
  QCOMPARE(switcherSpaceStepPosition(order, 9, 2, 4).status, Status::Elsewhere);
  QCOMPARE(switcherSpaceStepPosition(order, 0, 2, 4).status, Status::Elsewhere);
}

void OSXWindowSwitcherModelTests::spaceStepGate()
{
  const SwitcherStepTiming timing;
  auto at = [](double sincePost, bool landed, bool animating, bool saw) {
    SwitcherStepObservation o;
    o.sincePost = sincePost;
    o.landed = landed;
    o.animating = animating;
    o.sawAnimation = saw;
    return o;
  };
  // never faster than the minimum gap
  QCOMPARE(switcherStepGate(at(0.1, true, false, true), timing), SwitcherStepGate::Wait);
  // the transition runs: wait
  QCOMPARE(switcherStepGate(at(0.4, false, true, true), timing), SwitcherStepGate::Wait);
  // it ended: next step at once, without waiting for the late current Space
  QCOMPARE(switcherStepGate(at(0.3, false, false, true), timing), SwitcherStepGate::Ready);
  // the current Space showed it
  QCOMPARE(switcherStepGate(at(0.3, true, false, false), timing), SwitcherStepGate::Ready);
  // nothing known yet: wait (no overshoot), until the reported Space is trusted
  QCOMPARE(switcherStepGate(at(1.0, false, false, false), timing), SwitcherStepGate::Wait);
  QCOMPARE(switcherStepGate(at(1.5, false, false, false), timing), SwitcherStepGate::Resync);
  // a stuck transition flag does not stall the switch
  QCOMPARE(switcherStepGate(at(3.0, false, true, true), timing), SwitcherStepGate::Resync);
  QCOMPARE(switcherStepGate(at(3.0, true, true, true), timing), SwitcherStepGate::Ready);
}

void OSXWindowSwitcherModelTests::spacesSettled()
{
  const SwitcherStepTiming timing;
  auto at = [](double sinceStart, double sinceAction, bool animating, bool pendingLanded) {
    SwitcherSettleObservation o;
    o.sinceStart = sinceStart;
    o.sinceAction = sinceAction;
    o.animating = animating;
    o.pendingLanded = pendingLanded;
    return o;
  };
  QVERIFY(switcherSpacesSettled(at(0, -1, false, true), timing));
  QVERIFY(!switcherSpacesSettled(at(0, -1, true, true), timing));
  // a step was just posted
  QVERIFY(!switcherSpacesSettled(at(0, 0.3, false, true), timing));
  QVERIFY(switcherSpacesSettled(at(0.3, 0.6, false, true), timing));
  // the steps of the replaced switch have not shown up yet
  QVERIFY(!switcherSpacesSettled(at(0.5, 1.0, false, false), timing));
  QVERIFY(switcherSpacesSettled(at(1.0, 1.5, false, false), timing));
  // never stuck
  QVERIFY(switcherSpacesSettled(at(3.0, 0.1, true, false), timing));
}

void OSXWindowSwitcherModelTests::desktopHotkeys()
{
  QCOMPARE(switcherDesktopHotkeyId(1), 118);
  QCOMPARE(switcherDesktopHotkeyId(16), 133);
  QCOMPARE(switcherDesktopHotkeyId(0), 0);
  QCOMPARE(switcherDesktopHotkeyId(17), 0);

  // Control+1 (the default of Desktop 1)
  const auto control1 = switcherParseSymbolicHotkey(true, {65535, 18, 262144});
  QVERIFY(control1.has_value());
  QCOMPARE(control1->keyCode, uint16_t{18});
  QCOMPARE(control1->modifiers, uint64_t{0x40000});
  // unknown bits are dropped, fn is kept
  const auto withFn = switcherParseSymbolicHotkey(true, {65535, 19, 0x40000 | 0x800000 | 0x1});
  QVERIFY(withFn.has_value());
  QCOMPARE(withFn->modifiers, uint64_t{0x840000});
  // disabled, malformed, no key, no modifier, Shift only
  QVERIFY(!switcherParseSymbolicHotkey(false, {65535, 18, 262144}));
  QVERIFY(!switcherParseSymbolicHotkey(true, {65535, 18}));
  QVERIFY(!switcherParseSymbolicHotkey(true, {65535, 65535, 262144}));
  QVERIFY(!switcherParseSymbolicHotkey(true, {65535, 18, 0}));
  QVERIFY(!switcherParseSymbolicHotkey(true, {65535, 18, 0x20000}));

  // desktops are counted without the fullscreen Spaces between them
  const std::vector<SwitcherSpaceEntry> entries{
      {1, SwitcherSpaceKind::Desktop, {}},
      {1143, SwitcherSpaceKind::Fullscreen, {}},
      {5, SwitcherSpaceKind::Desktop, {}},
      {1149, SwitcherSpaceKind::Fullscreen, {}},
      {7, SwitcherSpaceKind::Desktop, {}},
  };
  QCOMPARE(switcherDesktopNumber(entries, 1), 1);
  QCOMPARE(switcherDesktopNumber(entries, 5), 2);
  QCOMPARE(switcherDesktopNumber(entries, 7), 3);
  QCOMPARE(switcherDesktopNumber(entries, 1143), 0);
  QCOMPARE(switcherDesktopNumber(entries, 99), 0);
}

void OSXWindowSwitcherModelTests::hotkeySwitchPlanner()
{
  const SwitcherHotkeyTiming timing;
  auto at = [](uint64_t current, double sinceStart, bool animating) {
    SwitcherHotkeyObservation o;
    o.current = current;
    o.sinceStart = sinceStart;
    o.animating = animating;
    return o;
  };
  // the current Space shows up late (about a second): wait for it
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1149, 0.1, false), timing), SwitcherHotkeyPlan::Wait);
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1149, 1.0, false), timing), SwitcherHotkeyPlan::Wait);
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1, 0.97, false), timing), SwitcherHotkeyPlan::Arrived);
  // the hotkey went somewhere else
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(5, 0.97, false), timing), SwitcherHotkeyPlan::Elsewhere);
  // unknown current Space is no landing
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(0, 0.5, false), timing), SwitcherHotkeyPlan::Wait);
  // nothing happened
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1149, 1.6, false), timing), SwitcherHotkeyPlan::NoEffect);
  // a running transition is waited for, but not forever
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1149, 2.0, true), timing), SwitcherHotkeyPlan::Wait);
  QCOMPARE(switcherPlanHotkeySwitch(1149, 1, at(1149, 3.0, true), timing), SwitcherHotkeyPlan::NoEffect);
}

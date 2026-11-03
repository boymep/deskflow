/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "platform/OSXWindowSwitcherSpaces.h"

#import "base/Log.h"

#import <ApplicationServices/ApplicationServices.h>
#import <dispatch/dispatch.h>
#import <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcherSpaces.mm must be compiled with ARC (-fobjc-arc)"
#endif

using deskflow::osx::SwitcherDirectMethod;
using deskflow::osx::SwitcherPid;
using deskflow::osx::SwitcherSpaceEntry;
using deskflow::osx::SwitcherSpaceKind;
using deskflow::osx::SwitcherTile;
using deskflow::osx::SwitcherWindowId;

namespace {

// --- private calls, all optional ---------------------------------------------------------------

using ConnectionFn = int32_t (*)();
using CopySpacesForWindowsFn = CFArrayRef (*)(int32_t connection, int32_t mask, CFArrayRef windowIds);
using CopyManagedDisplaySpacesFn = CFArrayRef (*)(int32_t connection);
using GetWindowFn = AXError (*)(AXUIElementRef, CGWindowID *);
using CreateWithRemoteTokenFn = AXUIElementRef (*)(CFDataRef);
using SetFrontProcessFn = CGError (*)(ProcessSerialNumber *, uint32_t windowId, uint32_t mode);
using PostEventRecordFn = CGError (*)(ProcessSerialNumber *, uint8_t *record);
using GetProcessForPidFn = OSStatus (*)(pid_t, ProcessSerialNumber *);
using WindowIsOrderedInFn = CGError (*)(int32_t connection, uint32_t windowId, bool *orderedIn);
using DisplayIsAnimatingFn = bool (*)(int32_t connection, CFStringRef displayIdentifier);

constexpr int32_t kAllSpacesMask = 0x7;        // current, other and user Spaces
constexpr int kUserSpaceType = 0;              // a Mission Control desktop
constexpr int kFullscreenSpaceType = 4;        // a fullscreen (or split view) app
constexpr uint32_t kUserGeneratedMode = 0x200; // front process change on behalf of the user
constexpr size_t kMaxSpaceQueries = 300;       // plausible windows looked up per open

void *lookup(std::initializer_list<const char *> names)
{
  static void *const skyLight =
      dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY | RTLD_LOCAL);
  for (const char *name : names) {
    if (skyLight != nullptr) {
      if (void *symbol = dlsym(skyLight, name); symbol != nullptr) {
        return symbol;
      }
    }
    if (void *symbol = dlsym(RTLD_DEFAULT, name); symbol != nullptr) {
      return symbol;
    }
  }
  return nullptr;
}

struct Spi
{
  ConnectionFn connection = reinterpret_cast<ConnectionFn>(lookup({"SLSMainConnectionID", "CGSMainConnectionID"}));
  CopySpacesForWindowsFn copySpacesForWindows =
      reinterpret_cast<CopySpacesForWindowsFn>(lookup({"SLSCopySpacesForWindows", "CGSCopySpacesForWindows"}));
  CopyManagedDisplaySpacesFn copyManagedDisplaySpaces = reinterpret_cast<CopyManagedDisplaySpacesFn>(
      lookup({"SLSCopyManagedDisplaySpaces", "CGSCopyManagedDisplaySpaces"})
  );
  GetWindowFn getWindow = reinterpret_cast<GetWindowFn>(lookup({"_AXUIElementGetWindow"}));
  CreateWithRemoteTokenFn createWithRemoteToken =
      reinterpret_cast<CreateWithRemoteTokenFn>(lookup({"_AXUIElementCreateWithRemoteToken"}));
  SetFrontProcessFn setFrontProcess = reinterpret_cast<SetFrontProcessFn>(lookup({"_SLPSSetFrontProcessWithOptions"}));
  PostEventRecordFn postEventRecord = reinterpret_cast<PostEventRecordFn>(lookup({"SLPSPostEventRecordTo"}));
  // optional: without it, windows an app keeps ordered out (closed to the menu bar) are listed too
  WindowIsOrderedInFn windowIsOrderedIn = reinterpret_cast<WindowIsOrderedInFn>(lookup({"SLSWindowIsOrderedIn"}));
  // deprecated public call, looked up to keep the build free of deprecation warnings
  GetProcessForPidFn getProcessForPid = reinterpret_cast<GetProcessForPidFn>(lookup({"GetProcessForPID"}));
  // optional: tells a direct Space switch that started from one that did nothing
  DisplayIsAnimatingFn displayIsAnimating =
      reinterpret_cast<DisplayIsAnimatingFn>(lookup({"SLSManagedDisplayIsAnimating", "CGSManagedDisplayIsAnimating"}));

  bool spaces() const
  {
    return connection != nullptr && copySpacesForWindows != nullptr && copyManagedDisplaySpaces != nullptr &&
           getWindow != nullptr;
  }
  bool focus() const
  {
    return setFrontProcess != nullptr && postEventRecord != nullptr && getProcessForPid != nullptr;
  }
};

const Spi &spi()
{
  static const Spi instance;
  return instance;
}

uint64_t numberValue(id value)
{
  return [value isKindOfClass:[NSNumber class]] ? [value unsignedLongLongValue] : 0;
}

uint64_t spaceIdOf(NSDictionary *space)
{
  if (![space isKindOfClass:[NSDictionary class]]) {
    return 0;
  }
  const uint64_t managed = numberValue(space[@"ManagedSpaceID"]);
  return managed != 0 ? managed : numberValue(space[@"id64"]);
}

SwitcherSpaceKind kindOf(NSDictionary *space)
{
  switch (static_cast<int>(numberValue(space[@"type"]))) {
  case kUserSpaceType:
    return SwitcherSpaceKind::Desktop;
  case kFullscreenSpaceType:
    return SwitcherSpaceKind::Fullscreen;
  default:
    return SwitcherSpaceKind::Other;
  }
}

//! Windows of a fullscreen Space: its tiles (split view), else its single window.
std::vector<SwitcherTile> tilesOf(NSDictionary *space)
{
  std::vector<SwitcherTile> tiles;
  NSDictionary *layout = space[@"TileLayoutManager"];
  NSArray *tileSpaces = [layout isKindOfClass:[NSDictionary class]] ? layout[@"TileSpaces"] : nil;
  if ([tileSpaces isKindOfClass:[NSArray class]]) {
    for (NSDictionary *tile in tileSpaces) {
      if (![tile isKindOfClass:[NSDictionary class]]) {
        continue;
      }
      const auto windowId = static_cast<SwitcherWindowId>(numberValue(tile[@"TileWindowID"]));
      if (windowId != 0) {
        tiles.push_back(SwitcherTile{static_cast<SwitcherPid>(numberValue(tile[@"pid"])), windowId});
      }
    }
  }
  if (tiles.empty()) {
    if (const auto windowId = static_cast<SwitcherWindowId>(numberValue(space[@"fs_wid"])); windowId != 0) {
      tiles.push_back(SwitcherTile{static_cast<SwitcherPid>(numberValue(space[@"pid"])), windowId});
    }
  }
  return tiles;
}

//! Spaces of one managed display in Mission Control order.
std::vector<SwitcherSpaceEntry> entriesOf(NSDictionary *display)
{
  std::vector<SwitcherSpaceEntry> entries;
  NSArray *spaces = [display isKindOfClass:[NSDictionary class]] ? display[@"Spaces"] : nil;
  if (![spaces isKindOfClass:[NSArray class]]) {
    return entries;
  }
  for (NSDictionary *space in spaces) {
    const uint64_t id = spaceIdOf(space);
    if (id == 0) {
      continue;
    }
    SwitcherSpaceEntry entry{id, kindOf(space), {}};
    if (entry.kind == SwitcherSpaceKind::Fullscreen) {
      entry.tiles = tilesOf(space);
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

//! The managed displays (Spaces of each display), or nil on failure.
NSArray *copyDisplays(int32_t connection)
{
  CFArrayRef raw = spi().copyManagedDisplaySpaces(connection);
  if (raw == nullptr) {
    return nil;
  }
  NSArray *displays = CFBridgingRelease(raw);
  return [displays isKindOfClass:[NSArray class]] ? displays : nil;
}

//! The UUID string of \p displayId, or nil.
NSString *displayUuid(uint32_t displayId)
{
  NSString *uuid = nil;
  if (CFUUIDRef ref = CGDisplayCreateUUIDFromDisplayID(displayId); ref != nullptr) {
    uuid = CFBridgingRelease(CFUUIDCreateString(kCFAllocatorDefault, ref));
    CFRelease(ref);
  }
  return uuid;
}

//! The managed display dictionary of \p displayId, or nil.
NSDictionary *managedDisplay(NSArray *displays, uint32_t displayId)
{
  NSString *uuid = displayUuid(displayId);
  NSDictionary *only = nil;
  NSUInteger count = 0;
  for (NSDictionary *display in displays) {
    if (![display isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    only = display;
    ++count;
    NSString *identifier = display[@"Display Identifier"];
    if ([identifier isKindOfClass:[NSString class]] &&
        (([identifier caseInsensitiveCompare:uuid ?: @""] == NSOrderedSame) ||
         ([identifier isEqualToString:@"Main"] && displayId == CGMainDisplayID()))) {
      return display;
    }
  }
  return count == 1 ? only : nil; // a single display is the one asked for
}

std::vector<uint64_t> spacesOf(int32_t connection, SwitcherWindowId windowId)
{
  std::vector<uint64_t> spaces;
  NSArray *ids = @[ @(windowId) ];
  CFArrayRef raw = spi().copySpacesForWindows(connection, kAllSpacesMask, (__bridge CFArrayRef)ids);
  if (raw == nullptr) {
    return spaces;
  }
  NSArray *result = CFBridgingRelease(raw);
  if ([result isKindOfClass:[NSArray class]]) {
    for (id space in result) {
      if (const uint64_t id = numberValue(space); id != 0) {
        spaces.push_back(id);
      }
    }
  }
  return spaces;
}

// --- remote Accessibility elements ------------------------------------------------------------

constexpr uint64_t kMaxElementId = 2000;       // element ids tried per app
constexpr CFAbsoluteTime kAppBudget = 0.25;    // seconds per app
constexpr CFAbsoluteTime kRunBudget = 1.0;     // seconds per search run
constexpr float kRemoteAxTimeout = 0.05f;      // per call; a hung app must not stall the search
constexpr int kMaxFailedCalls = 3;             // consecutive timeouts before an app is given up
constexpr CFAbsoluteTime kMissRetryDelay = 30; // seconds before a window not found is searched again

// --- closed-loop Space switch -----------------------------------------------------------------

//! Spaces of one display: the strip order (Control+Arrow order) and the current Space.
struct DisplaySpaces
{
  NSString *identifier;  //!< "Display Identifier" of the window server (a UUID or "Main")
  uint64_t current = 0;
  std::vector<SwitcherSpaceEntry> entries; //!< desktops and fullscreen Spaces, Mission Control order
  std::vector<uint64_t> order;             //!< their ids
};

bool readDisplaySpaces(uint32_t displayId, DisplaySpaces &out)
{
  NSDictionary *display = managedDisplay(copyDisplays(spi().connection()), displayId);
  if (display == nil) {
    return false;
  }
  out.current = spaceIdOf(display[@"Current Space"]);
  if (NSString *identifier = display[@"Display Identifier"]; [identifier isKindOfClass:[NSString class]]) {
    out.identifier = identifier;
  }
  for (auto &entry : entriesOf(display)) {
    if (entry.kind != SwitcherSpaceKind::Other) {
      out.order.push_back(entry.id);
      out.entries.push_back(std::move(entry));
    }
  }
  return out.current != 0;
}

//! Whether the display shows a Space transition. The window server may know it by its UUID even when
//! the managed display is listed as "Main", so both are asked.
bool displayAnimating(const DisplaySpaces &spaces, uint32_t displayId)
{
  if (spi().displayIsAnimating == nullptr) {
    return false;
  }
  const int32_t connection = spi().connection();
  if (spaces.identifier != nil && spi().displayIsAnimating(connection, (__bridge CFStringRef)spaces.identifier)) {
    return true;
  }
  NSString *uuid = displayUuid(displayId);
  return uuid != nil && ![uuid isEqualToString:spaces.identifier ?: @""] &&
         spi().displayIsAnimating(connection, (__bridge CFStringRef)uuid);
}

int msSince(CFAbsoluteTime start, CFAbsoluteTime now)
{
  return static_cast<int>((now - start) * 1000);
}

//! One Space switch in progress (main thread only). Owned by the scheduled blocks through a shared_ptr.
struct SpaceSwitch
{
  uint64_t id = 0; //!< number of this switch, for the log
  uint32_t displayId = 0;
  uint64_t target = 0;
  bool cancelled = false;   //!< replaced by a newer switch: its scheduled blocks do nothing
  CFAbsoluteTime askedAt = 0; //!< when the switch was asked for
  DeskflowSwitcherRemoteWindows *remote; //!< finds the element of the window to bring forward, or nil

  // stepping
  uint64_t confirmed = 0; //!< last Space reported current on the way; 0 before the first step
  uint64_t assumed = 0;   //!< Space the posted steps lead to; 0 before the first step
  CFAbsoluteTime steppingStart = 0;
  CFAbsoluteTime postedAt = 0;
  CFAbsoluteTime stepAnimationFrom = 0; //!< when a transition was first seen after the last step, 0 if not
  CFAbsoluteTime stepAnimationTo = 0;   //!< when it was last seen
  CFAbsoluteTime stepLandedAt = 0;      //!< when the last step showed up in the current Space, 0 if not yet
  bool stepLogged = false;              //!< the outcome of the last step is logged
  int steps = 0;                        //!< steps posted so far
  int resyncs = 0;                      //!< times the reported current Space was taken over

  // direct switch, before any step
  SwitcherPid pid = 0;              //!< app of the window that is brought forward
  SwitcherWindowId windowId = 0;    //!< that window
  ::id element;                     //!< its AXUIElementRef, or nil (the member \c id hides the type)
  uint64_t directFrom = 0;          //!< Space current when the direct switch started (or retargeted)
  std::vector<SwitcherDirectMethod> order;
  size_t tried = 0;                 //!< methods of \c order tried so far
  bool retargeted = false;          //!< the exact window was tried again after a wrong landing
  CFAbsoluteTime directStart = 0;
  CFAbsoluteTime methodAt = 0;      //!< when the last method was tried
  CFAbsoluteTime animationAt = 0;   //!< when a transition was last seen after it, 0 if never
  bool sawAnimation = false;        //!< a transition was seen at all
};

// main thread; the switch that is running. A new commit replaces it, which stops the old one.
std::shared_ptr<SpaceSwitch> g_activeSpaceSwitch;
uint64_t g_spaceSwitchCount = 0; // main thread
CFAbsoluteTime g_lastSpaceAction = 0; // main thread; last step or direct method of any switch, 0 if none
uint64_t g_pendingSpace = 0;          // main thread; where the steps of a replaced switch lead, 0 if none

constexpr CFAbsoluteTime kSpacePollInterval = 0.02; // how often the current Space is read while a step runs
constexpr int kMaxResyncs = 3;                      // times a late or lost step is recovered from

void postSpaceStep(int direction)
{
  constexpr CGKeyCode kLeftArrow = 123;
  constexpr CGKeyCode kRightArrow = 124;
  const CGKeyCode key = direction > 0 ? kRightArrow : kLeftArrow;
  for (const bool down : {true, false}) {
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key, down);
    if (event == nullptr) {
      return;
    }
    // same flags as the side buttons: macOS ignores a synthetic Control+Arrow without fn
    CGEventSetFlags(event, kCGEventFlagMaskControl | kCGEventFlagMaskSecondaryFn);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
  }
}

void continueSpaceSwitch(const std::shared_ptr<SpaceSwitch> &session);

void scheduleSpaceSwitch(std::shared_ptr<SpaceSwitch> session, CFAbsoluteTime delay)
{
  // the block keeps its own copy of the shared_ptr (a block captures a C++ reference as a reference,
  // so capturing a reference parameter here would leave the block with a dangling session)
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        continueSpaceSwitch(session);
      }
  );
}

void endSpaceSwitch(const std::shared_ptr<SpaceSwitch> &session)
{
  if (g_activeSpaceSwitch == session) {
    g_activeSpaceSwitch.reset();
  }
}

bool spaceSwitchLive(const std::shared_ptr<SpaceSwitch> &session)
{
  return !session->cancelled && g_activeSpaceSwitch == session;
}

//! Logs how the last step went: when its transition was seen and when the current Space showed it.
void logStepOutcome(const SpaceSwitch &session, CFAbsoluteTime now, const char *outcome)
{
  char transition[64] = "no transition seen";
  if (session.stepAnimationFrom > 0) {
    snprintf(
        transition, sizeof(transition), "transition %d-%d ms", msSince(session.postedAt, session.stepAnimationFrom),
        msSince(session.postedAt, session.stepAnimationTo)
    );
  }
  char landed[48] = "space not reported yet";
  if (session.stepLandedAt > 0) {
    snprintf(landed, sizeof(landed), "space reported at %d ms", msSince(session.postedAt, session.stepLandedAt));
  }
  LOG_INFO(
      "space switch #%llu: step %d %s after %d ms (%s, %s)", static_cast<unsigned long long>(session.id),
      session.steps, outcome, msSince(session.postedAt, now), transition, landed
  );
}

void continueSpaceSwitch(const std::shared_ptr<SpaceSwitch> &session)
{
  using Action = deskflow::osx::SwitcherSpaceStepPlan::Action;
  using Status = deskflow::osx::SwitcherSpacePosition::Status;
  using Gate = deskflow::osx::SwitcherStepGate;
  const auto id = static_cast<unsigned long long>(session->id);
  if (!spaceSwitchLive(session)) {
    return; // replaced: the newer switch logged it
  }
  DisplaySpaces spaces;
  if (!readDisplaySpaces(session->displayId, spaces)) {
    LOG_INFO("space switch #%llu: spaces unknown after %d steps, stopped", id, session->steps);
    endSpaceSwitch(session);
    return;
  }
  const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  if (session->steppingStart == 0) {
    session->steppingStart = now;
  }
  uint64_t position = spaces.current;
  if (session->assumed != 0) {
    // a step is out: wait until its transition is over, never past the target
    const bool animating = displayAnimating(spaces, session->displayId);
    if (animating) {
      if (session->stepAnimationFrom == 0) {
        session->stepAnimationFrom = now;
      }
      session->stepAnimationTo = now;
    }
    const auto where = deskflow::osx::switcherSpaceStepPosition(
        spaces.order, spaces.current, session->confirmed, session->assumed
    );
    if (where.status == Status::Elsewhere) {
      LOG_INFO(
          "space switch #%llu: stopped after %d steps, spaces were switched by something else (on %llu, expected %llu)",
          id, session->steps, static_cast<unsigned long long>(spaces.current),
          static_cast<unsigned long long>(session->assumed)
      );
      endSpaceSwitch(session);
      return;
    }
    const bool landed = spaces.current == session->assumed;
    if (landed && session->stepLandedAt == 0) {
      session->stepLandedAt = now;
    }
    session->confirmed = spaces.current; // on the way or there: a step back would show as elsewhere
    deskflow::osx::SwitcherStepObservation observation;
    observation.sincePost = now - session->postedAt;
    observation.landed = landed;
    observation.animating = animating;
    observation.sawAnimation = session->stepAnimationFrom > 0;
    const auto gate = deskflow::osx::switcherStepGate(observation);
    if (gate == Gate::Wait) {
      scheduleSpaceSwitch(session, kSpacePollInterval);
      return;
    }
    if (gate == Gate::Resync && !landed) {
      if (!session->stepLogged) {
        logStepOutcome(*session, now, "not confirmed");
      }
      if (++session->resyncs > kMaxResyncs) {
        LOG_INFO("space switch #%llu: stopped after %d steps, steps keep getting lost", id, session->steps);
        endSpaceSwitch(session);
        return;
      }
      LOG_INFO(
          "space switch #%llu: current space still reads %llu, going on from there", id,
          static_cast<unsigned long long>(spaces.current)
      );
      session->assumed = spaces.current;
      session->stepLogged = true;
    } else {
      if (!session->stepLogged) {
        logStepOutcome(*session, now, "over");
        session->stepLogged = true;
      }
      position = session->assumed;
    }
  }
  const auto plan = deskflow::osx::switcherPlanSpaceStep(spaces.order, position, session->target, 0);
  switch (plan.action) {
  case Action::Done:
    if (spaces.current == session->target) {
      LOG_INFO(
          "space switch #%llu: arrived after %d steps in %d ms", id, session->steps,
          msSince(session->steppingStart, now)
      );
      endSpaceSwitch(session);
      return;
    }
    // the last step leads there, the current Space is not reported yet: post nothing, wait for it
    scheduleSpaceSwitch(session, kSpacePollInterval);
    return;
  case Action::Abort:
    LOG_INFO("space switch #%llu: stopped after %d steps, current or target space unknown", id, session->steps);
    endSpaceSwitch(session);
    return;
  case Action::Step:
    break;
  }
  if (session->assumed == 0) {
    session->confirmed = spaces.current;
  }
  session->assumed = plan.expected;
  session->postedAt = now;
  session->stepAnimationFrom = 0;
  session->stepAnimationTo = 0;
  session->stepLandedAt = 0;
  session->stepLogged = false;
  ++session->steps;
  g_lastSpaceAction = now;
  LOG_INFO(
      "space switch #%llu: step %d to the %s at %d ms, %d left", id, session->steps,
      plan.direction > 0 ? "right" : "left", msSince(session->steppingStart, now), plan.remaining - 1
  );
  postSpaceStep(plan.direction);
  scheduleSpaceSwitch(session, kSpacePollInterval);
}

// --- direct Space switch ----------------------------------------------------------------------

constexpr CFAbsoluteTime kResolveBudget = 0.15; // searching the window's element when it is not cached
constexpr float kDirectAxTimeout = 0.1f;        // per Accessibility call of a direct method

//! The order the direct methods are tried in (main thread); the one that worked moves to the front.
std::vector<SwitcherDirectMethod> &directOrder()
{
  static std::vector<SwitcherDirectMethod> order = [] {
    const char *spec = std::getenv("DESKFLOW_SWITCHER_DIRECT_ORDER");
    return spec != nullptr ? deskflow::osx::switcherParseDirectMethods(spec)
                           : deskflow::osx::switcherDefaultDirectMethods();
  }();
  return order;
}

std::string orderName(const std::vector<SwitcherDirectMethod> &order)
{
  std::string name;
  for (const auto method : order) {
    name += deskflow::osx::switcherDirectMethodName(method);
  }
  return name.empty() ? "none" : name;
}

//! The front window of desktop \p spaceId that can be focused: on screen there, of a regular app.
std::optional<SwitcherTile> frontWindowOfDesktop(uint64_t spaceId)
{
  CFArrayRef raw =
      CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (raw == nullptr) {
    return std::nullopt;
  }
  NSArray<NSDictionary *> *list = CFBridgingRelease(raw);
  const int32_t connection = spi().connection();
  const pid_t ownPid = getpid();
  size_t queries = 0;
  for (NSDictionary *info in list) {
    if (numberValue(info[(__bridge NSString *)kCGWindowLayer]) != 0) {
      continue;
    }
    NSNumber *alpha = info[(__bridge NSString *)kCGWindowAlpha];
    if ([alpha isKindOfClass:[NSNumber class]] && alpha.doubleValue <= 0) {
      continue;
    }
    CGRect bounds{};
    if (NSDictionary *b = info[(__bridge NSString *)kCGWindowBounds]; ![b isKindOfClass:[NSDictionary class]] ||
        !CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)b, &bounds) || bounds.size.width < 50 ||
        bounds.size.height < 50) {
      continue;
    }
    const auto pid = static_cast<SwitcherPid>(numberValue(info[(__bridge NSString *)kCGWindowOwnerPID]));
    const auto windowId = static_cast<SwitcherWindowId>(numberValue(info[(__bridge NSString *)kCGWindowNumber]));
    if (pid <= 0 || pid == ownPid || windowId == 0) {
      continue;
    }
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (app == nil || app.activationPolicy != NSApplicationActivationPolicyRegular || app.hidden) {
      continue;
    }
    if (spi().windowIsOrderedIn != nullptr) {
      bool orderedIn = false;
      if (spi().windowIsOrderedIn(connection, windowId, &orderedIn) == kCGErrorSuccess && !orderedIn) {
        continue; // minimized or closed to the menu bar
      }
    }
    if (++queries > kMaxSpaceQueries) {
      break;
    }
    const auto spaces = spacesOf(connection, windowId);
    if (std::find(spaces.begin(), spaces.end(), spaceId) != spaces.end()) {
      return SwitcherTile{pid, windowId};
    }
  }
  return std::nullopt;
}

//! A: the app frontmost, the window main, focused and raised, all through Accessibility.
void focusByAccessibility(SwitcherPid pid, id element)
{
  if (AXUIElementRef app = AXUIElementCreateApplication(pid); app != nullptr) {
    AXUIElementSetMessagingTimeout(app, kDirectAxTimeout);
    AXUIElementSetAttributeValue(app, kAXFrontmostAttribute, kCFBooleanTrue);
    if (element != nil) {
      AXUIElementSetAttributeValue(app, kAXFocusedWindowAttribute, (__bridge CFTypeRef)element);
    }
    CFRelease(app);
  }
  if (element != nil) {
    auto window = (__bridge AXUIElementRef)element;
    AXUIElementSetMessagingTimeout(window, kDirectAxTimeout);
    AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
    AXUIElementSetAttributeValue(window, kAXFocusedAttribute, kCFBooleanTrue);
    AXUIElementPerformAction(window, kAXRaiseAction);
  }
}

//! B: the app activated like the Dock does, then A.
void focusByActivation(SwitcherPid pid, id element)
{
  NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
  if (app != nil && !app.terminated) {
    if (@available(macOS 14.0, *)) {
      // cooperative activation: hand over this process' activation, then ask for it from here
      if (NSApp != nil) {
        [NSApp yieldActivationToApplication:app];
      }
      [app activateFromApplication:NSRunningApplication.currentApplication options:NSApplicationActivateAllWindows];
    }
    [app activateWithOptions:NSApplicationActivateAllWindows];
  }
  focusByAccessibility(pid, element);
}

//! C: the window server makes the app the front process with this window and the window key, then AXRaise.
bool focusByWindowServer(SwitcherPid pid, SwitcherWindowId windowId, id element)
{
  if (!spi().focus() || windowId == 0) {
    return false;
  }
  ProcessSerialNumber psn{};
  if (spi().getProcessForPid(pid, &psn) != noErr) {
    return false;
  }
  if (spi().setFrontProcess(&psn, windowId, kUserGeneratedMode) != kCGErrorSuccess) {
    return false;
  }
  // the process is in front now; make the chosen window its key window
  for (const bool press : {true, false}) {
    auto record = deskflow::osx::switcherMakeKeyWindowEvent(windowId, press);
    spi().postEventRecord(&psn, record.data());
  }
  if (element != nil) {
    auto window = (__bridge AXUIElementRef)element;
    AXUIElementSetMessagingTimeout(window, kDirectAxTimeout);
    AXUIElementPerformAction(window, kAXRaiseAction);
  }
  return true;
}

//! After a wrong landing: the exact window main, focused and raised, then its app activated (without
//! bringing all its windows, which would pick another of them again).
void focusExactWindow(SwitcherPid pid, id element)
{
  auto window = (__bridge AXUIElementRef)element;
  AXUIElementSetMessagingTimeout(window, kDirectAxTimeout);
  AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
  AXUIElementSetAttributeValue(window, kAXFocusedAttribute, kCFBooleanTrue);
  AXUIElementPerformAction(window, kAXRaiseAction);
  NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
  if (app != nil && !app.terminated) {
    if (@available(macOS 14.0, *)) {
      if (NSApp != nil) {
        [NSApp yieldActivationToApplication:app];
      }
      [app activateFromApplication:NSRunningApplication.currentApplication options:0];
    } else {
      [app activateWithOptions:0];
    }
  }
}

void runDirectMethod(SwitcherDirectMethod method, const SpaceSwitch &session)
{
  switch (method) {
  case SwitcherDirectMethod::Accessibility:
    focusByAccessibility(session.pid, session.element);
    return;
  case SwitcherDirectMethod::Activation:
    focusByActivation(session.pid, session.element);
    return;
  case SwitcherDirectMethod::WindowServer:
    if (!focusByWindowServer(session.pid, session.windowId, session.element)) {
      LOG_INFO(
          "space switch #%llu: window server calls missing or refused", static_cast<unsigned long long>(session.id)
      );
    }
    return;
  }
}

void continueDirectSwitch(const std::shared_ptr<SpaceSwitch> &session);

void scheduleDirectSwitch(std::shared_ptr<SpaceSwitch> session, CFAbsoluteTime delay)
{
  // the block keeps its own copy of the shared_ptr (see scheduleSpaceSwitch)
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        continueDirectSwitch(session);
      }
  );
}

void continueDirectSwitch(const std::shared_ptr<SpaceSwitch> &session)
{
  using Action = deskflow::osx::SwitcherDirectPlan::Action;
  const auto id = static_cast<unsigned long long>(session->id);
  if (!spaceSwitchLive(session)) {
    return; // replaced: the newer switch logged it
  }
  DisplaySpaces spaces;
  if (!readDisplaySpaces(session->displayId, spaces)) {
    LOG_INFO("space switch #%llu: spaces unknown during the direct switch, stopped", id);
    endSpaceSwitch(session);
    return;
  }
  const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  deskflow::osx::SwitcherDirectObservation observation;
  observation.current = spaces.current;
  observation.animating = session->tried > 0 && displayAnimating(spaces, session->displayId);
  if (observation.animating) {
    session->animationAt = now;
    session->sawAnimation = true;
  }
  observation.sinceStart = now - session->directStart;
  observation.sinceMethod = now - session->methodAt;
  observation.sinceAnimation = session->animationAt > 0 ? now - session->animationAt : -1;
  observation.canRetarget = session->element != nil;
  observation.retargeted = session->retargeted;
  const auto plan = deskflow::osx::switcherPlanDirectSwitch(
      session->order, session->tried, session->directFrom, session->target, observation
  );
  const auto elapsedMs = static_cast<int>(observation.sinceStart * 1000);
  switch (plan.action) {
  case Action::Wait:
    scheduleDirectSwitch(session, kSpacePollInterval);
    return;
  case Action::Try:
    LOG_INFO(
        "space switch #%llu: trying direct method %s at %d ms", id,
        deskflow::osx::switcherDirectMethodName(plan.method), elapsedMs
    );
    runDirectMethod(plan.method, *session);
    ++session->tried;
    session->methodAt = CFAbsoluteTimeGetCurrent();
    session->animationAt = 0;
    g_lastSpaceAction = session->methodAt;
    scheduleDirectSwitch(session, kSpacePollInterval);
    return;
  case Action::Retarget:
    LOG_INFO(
        "space switch #%llu: direct via %s landed on space %llu after %d ms (%s), trying the exact window again", id,
        deskflow::osx::switcherDirectMethodName(session->order[session->tried - 1]),
        static_cast<unsigned long long>(spaces.current), elapsedMs,
        session->sawAnimation ? "transition seen" : "no transition seen"
    );
    focusExactWindow(session->pid, session->element);
    session->retargeted = true;
    session->directFrom = spaces.current;
    session->methodAt = CFAbsoluteTimeGetCurrent();
    session->animationAt = 0;
    g_lastSpaceAction = session->methodAt;
    scheduleDirectSwitch(session, kSpacePollInterval);
    return;
  case Action::Arrived: {
    if (session->tried == 0) {
      LOG_INFO("space switch #%llu: already there", id);
      endSpaceSwitch(session);
      return;
    }
    const auto method = session->order[session->tried - 1];
    directOrder() = deskflow::osx::switcherPromoteDirectMethod(directOrder(), method);
    LOG_INFO(
        "space switch #%llu: direct via %s%s, arrived after %d ms (%s)", id,
        deskflow::osx::switcherDirectMethodName(method), session->retargeted ? " and the exact window" : "", elapsedMs,
        session->sawAnimation ? "transition seen" : "no transition seen"
    );
    endSpaceSwitch(session);
    return;
  }
  case Action::FallBack:
    break;
  }
  if (session->retargeted || spaces.current != session->directFrom) {
    LOG_INFO(
        "space switch #%llu: direct methods landed on space %llu after %d ms%s, fell back to stepping", id,
        static_cast<unsigned long long>(spaces.current), elapsedMs,
        session->retargeted ? " (the exact window did not help)" : ""
    );
  } else {
    const std::vector<SwitcherDirectMethod> tried(
        session->order.begin(), session->order.begin() + static_cast<std::ptrdiff_t>(session->tried)
    );
    LOG_INFO(
        "space switch #%llu: direct methods %s failed after %d ms (%s), fell back to stepping", id,
        orderName(tried).c_str(),
        elapsedMs, session->sawAnimation ? "a transition did not finish" : "no transition"
    );
  }
  continueSpaceSwitch(session);
}

} // namespace

namespace deskflow::osx {

bool switcherIsSwitchableAXWindow(NSString *role, NSString *subrole, NSString *title)
{
  if (![role isEqualToString:(__bridge NSString *)kAXWindowRole]) {
    return false;
  }
  if ([subrole isEqualToString:(__bridge NSString *)kAXStandardWindowSubrole] ||
      [subrole isEqualToString:(__bridge NSString *)kAXDialogSubrole]) {
    return true;
  }
  // some toolkits report no or an unknown subrole for document windows; skip
  // floating panels and untitled utility windows
  return (subrole == nil || [subrole isEqualToString:(__bridge NSString *)kAXUnknownSubrole]) && title.length > 0;
}

bool switcherSpacesAvailable()
{
  return spi().spaces() && spi().focus();
}

SwitcherSpacesSnapshot switcherSpacesSnapshot(uint32_t displayId)
{
  SwitcherSpacesSnapshot snapshot;
  if (!switcherSpacesAvailable()) {
    return snapshot;
  }
  NSArray *displays = copyDisplays(spi().connection());
  if (displays == nil) {
    LOG_DEBUG("window switcher: Spaces unknown");
    return snapshot;
  }
  NSDictionary *display = managedDisplay(displays, displayId);
  if (display == nil) {
    return snapshot;
  }
  snapshot.strip = switcherSpaceStrip(entriesOf(display), spaceIdOf(display[@"Current Space"]));
  snapshot.valid = !snapshot.strip.empty();

  // window server titles of the fullscreen windows (empty without the Screen Recording permission)
  NSMutableArray<NSNumber *> *tileWindows = [NSMutableArray array];
  for (const auto &space : snapshot.strip) {
    for (const auto &tile : space.tiles) {
      [tileWindows addObject:@(tile.windowId)];
    }
  }
  if (tileWindows.count == 0) {
    return snapshot;
  }
  CFArrayRef raw = CGWindowListCreateDescriptionFromArray((__bridge CFArrayRef)tileWindows);
  if (raw == nullptr) {
    return snapshot;
  }
  NSArray *infos = CFBridgingRelease(raw);
  for (NSDictionary *info in infos) {
    id name = info[(__bridge NSString *)kCGWindowName];
    if ([name isKindOfClass:[NSString class]] && [name length] > 0) {
      const auto windowId = static_cast<SwitcherWindowId>(numberValue(info[(__bridge NSString *)kCGWindowNumber]));
      snapshot.tileTitles[windowId] = std::string([name UTF8String] ?: "");
    }
  }
  return snapshot;
}

std::vector<SwitcherTile> switcherVisibleFullscreenWindows()
{
  std::vector<SwitcherTile> result;
  if (!switcherSpacesAvailable()) {
    return result;
  }
  for (NSDictionary *display in copyDisplays(spi().connection())) {
    if (![display isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    NSDictionary *current = display[@"Current Space"];
    if ([current isKindOfClass:[NSDictionary class]] && kindOf(current) == SwitcherSpaceKind::Fullscreen) {
      const auto tiles = tilesOf(current);
      result.insert(result.end(), tiles.begin(), tiles.end());
    }
  }
  return result;
}

//! Starts \p session once the Spaces are settled: no transition running, none just asked for, and the
//! steps of a replaced switch shown in the current Space (or given up on).
static void startSpaceSwitchWhenSettled(const std::shared_ptr<SpaceSwitch> &session)
{
  if (!spaceSwitchLive(session)) {
    return;
  }
  const auto id = static_cast<unsigned long long>(session->id);
  const uint32_t displayId = session->displayId;
  DisplaySpaces spaces;
  if (!readDisplaySpaces(displayId, spaces)) {
    LOG_INFO("space switch #%llu: spaces of display %u unknown, nothing done", id, displayId);
    endSpaceSwitch(session);
    return;
  }
  const CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
  SwitcherSettleObservation observation;
  observation.sinceStart = now - session->askedAt;
  observation.sinceAction = g_lastSpaceAction > 0 ? now - g_lastSpaceAction : -1;
  observation.animating = displayAnimating(spaces, displayId);
  observation.pendingLanded = g_pendingSpace == 0 || spaces.current == g_pendingSpace;
  if (!switcherSpacesSettled(observation)) {
    // the block keeps its own copy of the shared_ptr (see scheduleSpaceSwitch)
    std::shared_ptr<SpaceSwitch> waiting = session;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(kSpacePollInterval * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
          startSpaceSwitchWhenSettled(waiting);
        }
    );
    return;
  }
  if (now - session->askedAt > kSpacePollInterval) {
    LOG_INFO(
        "space switch #%llu: waited %d ms for the previous transition to settle%s", id, msSince(session->askedAt, now),
        observation.pendingLanded ? "" : " (its steps never showed up)"
    );
  }
  g_pendingSpace = 0;
  const uint64_t spaceId = session->target;
  const auto target = std::find_if(spaces.entries.begin(), spaces.entries.end(), [spaceId = session->target](const auto &entry) {
    return entry.id == spaceId;
  });
  const char *kind = target == spaces.entries.end()                  ? "unknown"
                     : target->kind == SwitcherSpaceKind::Fullscreen ? "fullscreen"
                                                                     : "desktop";
  // the window to bring forward: the (first) window of a fullscreen Space, the front window of a desktop
  std::optional<SwitcherTile> window;
  if (target != spaces.entries.end() && spaces.current != spaceId && !directOrder().empty()) {
    if (target->kind == SwitcherSpaceKind::Fullscreen) {
      if (!target->tiles.empty()) {
        window = target->tiles.front();
      }
    } else {
      window = frontWindowOfDesktop(spaceId);
    }
  }
  if (!window) {
    LOG_INFO(
        "space switch #%llu: to %s space %llu from %llu by stepping%s", id, kind,
        static_cast<unsigned long long>(spaceId), static_cast<unsigned long long>(spaces.current),
        target != spaces.entries.end() && target->kind == SwitcherSpaceKind::Desktop && spaces.current != spaceId &&
                !directOrder().empty()
            ? " (no window on it)"
            : ""
    );
    continueSpaceSwitch(session);
    return;
  }
  session->pid = window->pid;
  session->windowId = window->windowId;
  session->directFrom = spaces.current;
  session->order = directOrder();
  session->directStart = CFAbsoluteTimeGetCurrent();
  if (session->remote != nil) {
    session->element = [session->remote windowNow:window->windowId pid:window->pid budget:kResolveBudget].element;
  }
  LOG_INFO(
      "space switch #%llu: to %s space %llu from %llu directly (window %u of pid %d, %s element, order %s, %s)", id,
      kind, static_cast<unsigned long long>(spaceId), static_cast<unsigned long long>(spaces.current),
      window->windowId, static_cast<int>(window->pid), session->element != nil ? "with" : "no",
      orderName(session->order).c_str(),
      spi().displayIsAnimating != nullptr ? "transition detection on" : "no transition detection"
  );
  continueDirectSwitch(session);
}

void switcherSwitchToSpace(uint32_t displayId, uint64_t spaceId, DeskflowSwitcherRemoteWindows *remote)
{
  // a new commit: it replaces (and so stops at once) a switch that is still running
  auto session = std::make_shared<SpaceSwitch>();
  session->id = ++g_spaceSwitchCount;
  session->displayId = displayId;
  session->target = spaceId;
  session->remote = remote;
  session->askedAt = CFAbsoluteTimeGetCurrent();
  if (const auto previous = g_activeSpaceSwitch; previous != nullptr) {
    previous->cancelled = true;
    LOG_INFO(
        "space switch #%llu: replaced by #%llu after %d steps", static_cast<unsigned long long>(previous->id),
        static_cast<unsigned long long>(session->id), previous->steps
    );
    if (previous->assumed != 0 && previous->assumed != previous->confirmed) {
      g_pendingSpace = previous->assumed; // its last steps are still on their way
    }
  }
  g_activeSpaceSwitch = session;
  if (!switcherSpacesAvailable()) {
    LOG_INFO(
        "space switch #%llu: spaces of display %u unknown, nothing done", static_cast<unsigned long long>(session->id),
        displayId
    );
    endSpaceSwitch(session);
    return;
  }
  startSpaceSwitchWhenSettled(session);
}

} // namespace deskflow::osx

@implementation DeskflowSwitcherRemoteWindow
@end

@implementation DeskflowSwitcherRemoteWindows {
  dispatch_queue_t _queue;
  NSMutableDictionary<NSNumber *, DeskflowSwitcherRemoteWindow *> *_cache;
  NSMutableSet<NSNumber *> *_rejected;
  NSMutableSet<NSNumber *> *_searching;
  NSMutableDictionary<NSNumber *, NSDate *> *_missed;
}

+ (BOOL)available
{
  return spi().createWithRemoteToken != nullptr && spi().getWindow != nullptr;
}

- (instancetype)init
{
  if ((self = [super init])) {
    _queue = dispatch_queue_create(
        "org.deskflow.switcher.remote-windows",
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0)
    );
    _cache = [NSMutableDictionary dictionary];
    _rejected = [NSMutableSet set];
    _searching = [NSMutableSet set];
    _missed = [NSMutableDictionary dictionary];
  }
  return self;
}

- (DeskflowSwitcherRemoteWindow *)cachedWindow:(uint32_t)windowId
{
  return _cache[@(windowId)];
}

- (DeskflowSwitcherRemoteWindow *)windowNow:(uint32_t)windowId pid:(pid_t)pid budget:(CFAbsoluteTime)budget
{
  NSNumber *key = @(windowId);
  if (DeskflowSwitcherRemoteWindow *cached = _cache[key]; cached != nil || windowId == 0 ||
                                                         ![DeskflowSwitcherRemoteWindows available]) {
    return cached;
  }
  NSMutableDictionary<NSNumber *, DeskflowSwitcherRemoteWindow *> *results = [NSMutableDictionary dictionary];
  NSMutableSet<NSNumber *> *rejected = [NSMutableSet set];
  [DeskflowSwitcherRemoteWindows searchApp:pid
                                   windows:std::vector<SwitcherWindowId>{windowId}
                                  deadline:CFAbsoluteTimeGetCurrent() + budget
                                   results:results
                                  rejected:rejected];
  DeskflowSwitcherRemoteWindow *window = results[key];
  if (window != nil) {
    _cache[key] = window;
  }
  return window;
}

- (void)retainWindows:(const std::vector<SwitcherWindowId> &)alive
{
  NSMutableSet<NSNumber *> *keep = [NSMutableSet setWithCapacity:alive.size()];
  for (const auto id : alive) {
    [keep addObject:@(id)];
  }
  for (NSNumber *id in _cache.allKeys) {
    if (![keep containsObject:id]) {
      [_cache removeObjectForKey:id];
    }
  }
  for (NSNumber *id in _missed.allKeys) {
    if (![keep containsObject:id]) {
      [_missed removeObjectForKey:id];
    }
  }
  // rejected ids are not reported as alive (they are filtered out), so keep them bounded instead
  if (_rejected.count > 500) {
    [_rejected removeAllObjects];
  }
}

- (void)resolve:(const std::vector<std::pair<SwitcherPid, SwitcherWindowId>> &)wanted
          found:(void (^)(uint32_t, DeskflowSwitcherRemoteWindow *))found
{
  if (![DeskflowSwitcherRemoteWindows available]) {
    return;
  }
  NSDate *now = [NSDate date];
  std::map<SwitcherPid, std::vector<SwitcherWindowId>> byApp;
  for (const auto &[pid, windowId] : wanted) {
    NSNumber *key = @(windowId);
    if (windowId == 0 || _cache[key] != nil || [_rejected containsObject:key] || [_searching containsObject:key]) {
      continue;
    }
    if (NSDate *missed = _missed[key]; missed != nil && [now timeIntervalSinceDate:missed] < kMissRetryDelay) {
      continue;
    }
    [_searching addObject:key];
    byApp[pid].push_back(windowId);
  }
  if (byApp.empty()) {
    return;
  }

  __weak DeskflowSwitcherRemoteWindows *weakSelf = self;
  dispatch_async(_queue, ^{
    @autoreleasepool {
      const CFAbsoluteTime runDeadline = CFAbsoluteTimeGetCurrent() + kRunBudget;
      NSMutableDictionary<NSNumber *, DeskflowSwitcherRemoteWindow *> *results = [NSMutableDictionary dictionary];
      NSMutableSet<NSNumber *> *rejected = [NSMutableSet set];
      for (const auto &[pid, ids] : byApp) {
        if (CFAbsoluteTimeGetCurrent() >= runDeadline) {
          break;
        }
        [DeskflowSwitcherRemoteWindows searchApp:pid
                                         windows:ids
                                        deadline:std::min(runDeadline, CFAbsoluteTimeGetCurrent() + kAppBudget)
                                         results:results
                                        rejected:rejected];
        // hand out each app's windows as soon as they are known
        NSDictionary *batch = [results copy];
        [results removeAllObjects];
        if (batch.count > 0) {
          dispatch_async(dispatch_get_main_queue(), ^{
            DeskflowSwitcherRemoteWindows *strongSelf = weakSelf;
            if (strongSelf == nil) {
              return;
            }
            [batch enumerateKeysAndObjectsUsingBlock:^(NSNumber *key, DeskflowSwitcherRemoteWindow *window, BOOL *) {
              strongSelf->_cache[key] = window;
              found(key.unsignedIntValue, window);
            }];
          });
        }
      }
      NSSet *rejectedCopy = [rejected copy];
      dispatch_async(dispatch_get_main_queue(), ^{
        DeskflowSwitcherRemoteWindows *strongSelf = weakSelf;
        if (strongSelf == nil) {
          return;
        }
        [strongSelf->_rejected unionSet:rejectedCopy];
        NSDate *done = [NSDate date];
        for (const auto &[pid, ids] : byApp) {
          for (const auto id : ids) {
            NSNumber *key = @(id);
            [strongSelf->_searching removeObject:key];
            if (strongSelf->_cache[key] == nil && ![rejectedCopy containsObject:key]) {
              strongSelf->_missed[key] = done;
            }
          }
        }
      });
    }
  });
}

//! Background queue. Tries element ids of \p pid until every window in \p ids is found or \p deadline passes.
+ (void)searchApp:(SwitcherPid)pid
          windows:(const std::vector<SwitcherWindowId> &)ids
         deadline:(CFAbsoluteTime)deadline
          results:(NSMutableDictionary<NSNumber *, DeskflowSwitcherRemoteWindow *> *)results
         rejected:(NSMutableSet<NSNumber *> *)rejected
{
  NSArray *attributes = @[
    (__bridge NSString *)kAXRoleAttribute, (__bridge NSString *)kAXSubroleAttribute,
    (__bridge NSString *)kAXTitleAttribute, (__bridge NSString *)kAXMinimizedAttribute
  ];
  size_t remaining = ids.size();
  int failedCalls = 0;
  for (uint64_t elementId = 0; elementId < kMaxElementId && remaining > 0; ++elementId) {
    if (CFAbsoluteTimeGetCurrent() >= deadline || failedCalls >= kMaxFailedCalls) {
      break;
    }
    const auto token = deskflow::osx::switcherRemoteToken(pid, elementId);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, token.data(), static_cast<CFIndex>(token.size()));
    if (data == nullptr) {
      break;
    }
    AXUIElementRef raw = spi().createWithRemoteToken(data);
    CFRelease(data);
    if (raw == nullptr) {
      continue;
    }
    id element = CFBridgingRelease(raw);
    auto ax = (__bridge AXUIElementRef)element;
    AXUIElementSetMessagingTimeout(ax, kRemoteAxTimeout);
    CFArrayRef rawValues = nullptr;
    const AXError error = AXUIElementCopyMultipleAttributeValues(ax, (__bridge CFArrayRef)attributes, 0, &rawValues);
    if (error == kAXErrorCannotComplete) {
      ++failedCalls; // app busy or not answering
      continue;
    }
    failedCalls = 0;
    if (error != kAXErrorSuccess || rawValues == nullptr) {
      continue;
    }
    NSArray *values = CFBridgingRelease(rawValues);
    auto stringAt = ^NSString *(NSUInteger i) {
      id value = i < values.count ? values[i] : nil;
      return [value isKindOfClass:[NSString class]] ? value : nil;
    };
    NSString *role = stringAt(0);
    if (![role isEqualToString:(__bridge NSString *)kAXWindowRole]) {
      continue;
    }
    CGWindowID windowId = 0;
    if (spi().getWindow(ax, &windowId) != kAXErrorSuccess || std::find(ids.begin(), ids.end(), windowId) == ids.end()) {
      continue;
    }
    NSNumber *key = @(windowId);
    if (results[key] != nil || [rejected containsObject:key]) {
      continue;
    }
    --remaining;
    NSString *title = stringAt(2) ?: @"";
    if (!deskflow::osx::switcherIsSwitchableAXWindow(role, stringAt(1), title)) {
      [rejected addObject:key];
      continue;
    }
    DeskflowSwitcherRemoteWindow *window = [DeskflowSwitcherRemoteWindow new];
    window.element = element;
    window.title = title;
    id minimized = values.count > 3 ? values[3] : nil;
    window.minimized = [minimized isKindOfClass:[NSNumber class]] && [minimized boolValue];
    results[key] = window;
  }
}

@end

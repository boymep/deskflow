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

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcherSpaces.mm must be compiled with ARC (-fobjc-arc)"
#endif

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

//! The managed display dictionary of \p displayId, or nil.
NSDictionary *managedDisplay(NSArray *displays, uint32_t displayId)
{
  NSString *uuid = nil;
  if (CFUUIDRef ref = CGDisplayCreateUUIDFromDisplayID(displayId); ref != nullptr) {
    uuid = CFBridgingRelease(CFUUIDCreateString(kCFAllocatorDefault, ref));
    CFRelease(ref);
  }
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

SwitcherSpacesSnapshot switcherSpacesSnapshot(
    uint32_t displayId, const std::vector<SwitcherPid> &allowed, const std::vector<SwitcherWindowId> &axSwitchable,
    const std::vector<SwitcherPid> &axAnswered, size_t limit
)
{
  SwitcherSpacesSnapshot snapshot;
  if (!switcherSpacesAvailable()) {
    return snapshot;
  }
  const int32_t connection = spi().connection();
  NSArray *displays = copyDisplays(connection);
  if (displays == nil) {
    LOG_DEBUG("window switcher: Spaces unknown, listing the current Space only");
    return snapshot;
  }
  std::vector<uint64_t> desktopSpaces;
  std::vector<uint64_t> visibleSpaces;
  std::vector<SwitcherWindowId> tileWindows;
  for (NSDictionary *display in displays) {
    if (![display isKindOfClass:[NSDictionary class]]) {
      continue;
    }
    if (const uint64_t current = spaceIdOf(display[@"Current Space"]); current != 0) {
      visibleSpaces.push_back(current);
    }
    for (const auto &entry : entriesOf(display)) {
      if (entry.kind == SwitcherSpaceKind::Desktop) {
        desktopSpaces.push_back(entry.id);
      }
      for (const auto &tile : entry.tiles) {
        tileWindows.push_back(tile.windowId);
      }
    }
  }
  if (NSDictionary *display = managedDisplay(displays, displayId); display != nil) {
    snapshot.strip = switcherSpaceStrip(entriesOf(display), spaceIdOf(display[@"Current Space"]));
  }
  if (desktopSpaces.empty() || visibleSpaces.empty()) {
    LOG_DEBUG("window switcher: Spaces unknown, listing the current Space only");
    return snapshot;
  }

  CFArrayRef raw =
      CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (raw == nullptr) {
    return snapshot;
  }
  NSArray *infos = CFBridgingRelease(raw);
  std::vector<SwitcherSpaceWindow> windows;
  std::vector<NSString *> titles;
  for (NSDictionary *info in infos) {
    SwitcherSpaceWindow window;
    window.windowId = static_cast<SwitcherWindowId>(numberValue(info[(__bridge NSString *)kCGWindowNumber]));
    id name = info[(__bridge NSString *)kCGWindowName];
    NSString *title = [name isKindOfClass:[NSString class]] ? name : @"";
    if (std::find(tileWindows.begin(), tileWindows.end(), window.windowId) != tileWindows.end()) {
      if (title.length > 0) {
        snapshot.tileTitles[window.windowId] = std::string(title.UTF8String ?: "");
      }
      continue; // shown by its strip item
    }
    if (windows.size() >= kMaxSpaceQueries) {
      continue;
    }
    window.pid = static_cast<SwitcherPid>([info[(__bridge NSString *)kCGWindowOwnerPID] intValue]);
    window.layer = [info[(__bridge NSString *)kCGWindowLayer] intValue];
    id alpha = info[(__bridge NSString *)kCGWindowAlpha];
    window.alpha = [alpha isKindOfClass:[NSNumber class]] ? [alpha doubleValue] : 1.0;
    CGRect bounds = CGRectZero;
    if (NSDictionary *boundsDict = info[(__bridge NSString *)kCGWindowBounds];
        [boundsDict isKindOfClass:[NSDictionary class]]) {
      CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)boundsDict, &bounds);
    }
    window.frame = {bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height};
    if (!switcherSpaceWindowPlausible(window, allowed)) {
      continue;
    }
    if (spi().windowIsOrderedIn != nullptr) {
      bool orderedIn = true;
      if (spi().windowIsOrderedIn(connection, window.windowId, &orderedIn) == kCGErrorSuccess) {
        window.orderedIn = orderedIn;
      }
    }
    if (window.orderedIn) {
      window.spaces = spacesOf(connection, window.windowId);
    }
    titles.push_back(title);
    windows.push_back(std::move(window));
  }

  snapshot.valid = true;
  for (const auto index :
       selectSwitcherDesktopWindows(windows, allowed, desktopSpaces, visibleSpaces, axSwitchable, axAnswered, limit)) {
    const auto &w = windows[index];
    const bool visible = std::any_of(w.spaces.begin(), w.spaces.end(), [&](uint64_t space) {
      return std::find(visibleSpaces.begin(), visibleSpaces.end(), space) != visibleSpaces.end();
    });
    snapshot.windows.push_back(
        SwitcherSpaceWindowInfo{w.windowId, w.pid, std::string(titles[index].UTF8String ?: ""), w.frame, visible}
    );
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

void switcherSwitchSpaces(int steps)
{
  constexpr CGKeyCode kLeftArrow = 123;
  constexpr CGKeyCode kRightArrow = 124;
  constexpr int64_t kStepDelayMs = 350;
  if (steps == 0) {
    return;
  }
  const CGKeyCode key = steps > 0 ? kRightArrow : kLeftArrow;
  for (int i = 0; i < std::abs(steps); ++i) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, i * kStepDelayMs * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
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
    });
  }
}

bool switcherFocusWindow(SwitcherPid pid, SwitcherWindowId windowId)
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
    auto record = switcherMakeKeyWindowEvent(windowId, press);
    spi().postEventRecord(&psn, record.data());
  }
  return true;
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

- (BOOL)isRejected:(uint32_t)windowId
{
  return [_rejected containsObject:@(windowId)];
}

- (void)reject:(const std::vector<SwitcherWindowId> &)windowIds
{
  for (const auto id : windowIds) {
    if (id != 0) {
      [_rejected addObject:@(id)];
    }
  }
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

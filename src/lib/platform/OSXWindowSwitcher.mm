/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "platform/OSXWindowSwitcher.h"

#import "base/Log.h"
#import "platform/OSXWindowSwitcherModel.h"
#import "platform/OSXWindowSwitcherPanel.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>
#import <dlfcn.h>
#import <unistd.h>

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcher.mm must be compiled with ARC (-fobjc-arc)"
#endif

using deskflow::osx::SwitcherCandidate;
using deskflow::osx::SwitcherCommand;
using deskflow::osx::SwitcherMru;
using deskflow::osx::SwitcherWindowId;

namespace {

constexpr NSUInteger kMaxWindows = 200;       // whole list
constexpr CFIndex kMaxWindowsPerApp = 50;     // per application
constexpr float kAxTimeoutSeconds = 0.1f;     // a hung app must not freeze the switcher
constexpr int64_t kIdleTimeoutSeconds = 60;   // safety net: never leave the panel up forever

// private but long-standing HIServices call mapping an AX window to its CGWindowID;
// looked up at runtime so a missing symbol only disables window ids
using GetWindowFn = AXError (*)(AXUIElementRef, CGWindowID *);

GetWindowFn getWindowFn()
{
  static GetWindowFn fn = reinterpret_cast<GetWindowFn>(dlsym(RTLD_DEFAULT, "_AXUIElementGetWindow"));
  return fn;
}

SwitcherWindowId windowIdOf(AXUIElementRef window)
{
  CGWindowID id = 0;
  if (const auto fn = getWindowFn(); fn != nullptr && window != nullptr && fn(window, &id) == kAXErrorSuccess) {
    return id;
  }
  return 0;
}

id copyAttribute(AXUIElementRef element, CFStringRef attribute)
{
  CFTypeRef value = nullptr;
  if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) {
    return nil;
  }
  return CFBridgingRelease(value);
}

AXUIElementRef createAppElement(pid_t pid)
{
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (app != nullptr) {
    AXUIElementSetMessagingTimeout(app, kAxTimeoutSeconds);
  }
  return app;
}

// windows on screen at the normal window level, front to back
std::vector<SwitcherWindowId> onScreenWindowOrder()
{
  std::vector<SwitcherWindowId> order;
  CFArrayRef info = CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID
  );
  if (info == nullptr) {
    return order;
  }
  for (NSDictionary *window in (__bridge NSArray *)info) {
    if ([window[(__bridge NSString *)kCGWindowLayer] integerValue] != 0) {
      continue;
    }
    order.push_back(static_cast<SwitcherWindowId>([window[(__bridge NSString *)kCGWindowNumber] unsignedIntValue]));
  }
  CFRelease(info);
  return order;
}

bool isSwitchableWindow(NSString *role, NSString *subrole, NSString *title)
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

} // namespace

//! Main thread only.
@interface DeskflowWindowSwitcherController : NSObject
- (void)handle:(SwitcherCommand)command;
- (void)shutdown;
@end

@implementation DeskflowWindowSwitcherController {
  SwitcherMru _mru;
  DeskflowSwitcherPanel *_panel;
  NSArray<DeskflowSwitcherItem *> *_items;
  NSUInteger _selection;
  BOOL _open;
  uint64_t _generation;
  NSMutableArray<id> *_observers;
  BOOL _warnedUntrusted;
}

- (instancetype)init
{
  if ((self = [super init])) {
    _panel = [DeskflowSwitcherPanel new];
    _items = @[];
    _observers = [NSMutableArray array];
    [self startObserving];
  }
  return self;
}

- (void)startObserving
{
  NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
  __weak DeskflowWindowSwitcherController *weakSelf = self;
  [_observers addObject:[center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                            object:nil
                                             queue:NSOperationQueue.mainQueue
                                        usingBlock:^(NSNotification *note) {
                                          NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                                          [weakSelf noteActivated:app];
                                        }]];
  [_observers addObject:[center addObserverForName:NSWorkspaceDidTerminateApplicationNotification
                                            object:nil
                                             queue:NSOperationQueue.mainQueue
                                        usingBlock:^(NSNotification *note) {
                                          NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                                          [weakSelf noteTerminated:app];
                                        }]];
  [self noteActivated:NSWorkspace.sharedWorkspace.frontmostApplication];
}

- (void)shutdown
{
  [self cancel];
  for (id observer in _observers) {
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer];
  }
  [_observers removeAllObjects];
}

- (void)noteActivated:(NSRunningApplication *)app
{
  if (app == nil || app.processIdentifier == getpid()) {
    return;
  }
  _mru.touchWindow(app.processIdentifier, [self focusedWindowIdOf:app.processIdentifier]);
}

- (void)noteTerminated:(NSRunningApplication *)app
{
  if (app != nil) {
    _mru.removeApp(app.processIdentifier);
  }
}

- (SwitcherWindowId)focusedWindowIdOf:(pid_t)pid
{
  if (!AXIsProcessTrusted()) {
    return 0;
  }
  AXUIElementRef app = createAppElement(pid);
  if (app == nullptr) {
    return 0;
  }
  id focused = copyAttribute(app, kAXFocusedWindowAttribute);
  CFRelease(app);
  return focused != nil ? windowIdOf((__bridge AXUIElementRef)focused) : 0;
}

- (void)handle:(SwitcherCommand)command
{
  switch (command) {
  case SwitcherCommand::Open:
  case SwitcherCommand::OpenReverse:
    [self open:command == SwitcherCommand::OpenReverse];
    break;
  case SwitcherCommand::Next:
    [self step:1];
    break;
  case SwitcherCommand::Prev:
    [self step:-1];
    break;
  case SwitcherCommand::Commit:
    [self commit];
    break;
  case SwitcherCommand::Cancel:
    [self cancel];
    break;
  case SwitcherCommand::None:
    break;
  }
}

- (void)ensureApplication
{
  // deskflow-core runs QApplication, so NSApp normally exists; it is a background
  // (prohibited) process, which may not show windows: become an accessory app
  // (still no Dock icon, never activated by us)
  NSApplication *app = [NSApplication sharedApplication];
  if (app.activationPolicy == NSApplicationActivationPolicyProhibited) {
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
  }
}

- (NSArray<DeskflowSwitcherItem *> *)collectItems
{
  NSMutableArray<DeskflowSwitcherItem *> *items = [NSMutableArray array];
  const pid_t ownPid = getpid();
  for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
    if (items.count >= kMaxWindows) {
      break;
    }
    if (app.activationPolicy != NSApplicationActivationPolicyRegular || app.terminated ||
        app.processIdentifier == ownPid) {
      continue;
    }
    AXUIElementRef axApp = createAppElement(app.processIdentifier);
    if (axApp == nullptr) {
      continue;
    }
    NSArray *windows = copyAttribute(axApp, kAXWindowsAttribute);
    CFRelease(axApp);
    if (![windows isKindOfClass:[NSArray class]]) {
      continue;
    }
    NSString *appName = app.localizedName ?: @"";
    int index = 0;
    for (id window in windows) {
      if (index >= kMaxWindowsPerApp || items.count >= kMaxWindows) {
        break;
      }
      auto element = (__bridge AXUIElementRef)window;
      AXUIElementSetMessagingTimeout(element, kAxTimeoutSeconds);
      NSArray *attributes = @[
        (__bridge NSString *)kAXRoleAttribute, (__bridge NSString *)kAXSubroleAttribute,
        (__bridge NSString *)kAXTitleAttribute, (__bridge NSString *)kAXMinimizedAttribute
      ];
      CFArrayRef rawValues = nullptr;
      if (AXUIElementCopyMultipleAttributeValues(element, (__bridge CFArrayRef)attributes, 0, &rawValues) !=
              kAXErrorSuccess ||
          rawValues == nullptr) {
        continue;
      }
      NSArray *values = CFBridgingRelease(rawValues);
      auto stringAt = ^NSString *(NSUInteger i) {
        id value = i < values.count ? values[i] : nil;
        return [value isKindOfClass:[NSString class]] ? value : nil;
      };
      NSString *role = stringAt(0);
      NSString *subrole = stringAt(1);
      NSString *title = stringAt(2) ?: @"";
      id minimized = values.count > 3 ? values[3] : nil;
      if (!isSwitchableWindow(role, subrole, title)) {
        continue;
      }

      DeskflowSwitcherItem *item = [DeskflowSwitcherItem new];
      item.title = title;
      item.appName = appName;
      item.icon = app.icon;
      item.minimized = [minimized isKindOfClass:[NSNumber class]] && [minimized boolValue];
      item.appHidden = app.hidden;
      item.pid = app.processIdentifier;
      item.windowId = windowIdOf(element);
      item.axWindow = window;
      [items addObject:item];
      ++index;
    }
  }
  return items;
}

- (void)open:(BOOL)reverse
{
  [self hidePanel];
  if (!AXIsProcessTrusted()) {
    if (!_warnedUntrusted) {
      LOG_WARN("window switcher: accessibility permission missing, cannot list windows");
      _warnedUntrusted = YES;
    }
    return;
  }
  [self ensureApplication];

  const auto started = CFAbsoluteTimeGetCurrent();
  // the frontmost window may have changed without an app activation (e.g. Command+`)
  if (NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication; front != nil) {
    [self noteActivated:front];
  }

  NSArray<DeskflowSwitcherItem *> *found = [self collectItems];
  std::vector<SwitcherCandidate> candidates;
  std::vector<SwitcherWindowId> alive;
  candidates.reserve(found.count);
  for (DeskflowSwitcherItem *item in found) {
    // windows of the same app are reported front to back; index counts within the app
    int appIndex = 0;
    for (const auto &c : candidates) {
      if (c.pid == item.pid) {
        ++appIndex;
      }
    }
    candidates.push_back(SwitcherCandidate{item.pid, item.windowId, appIndex, static_cast<bool>(item.minimized)});
    if (item.windowId != 0) {
      alive.push_back(item.windowId);
    }
  }
  _mru.retainWindows(alive);
  const auto order = deskflow::osx::orderSwitcherCandidates(candidates, onScreenWindowOrder(), _mru);
  NSMutableArray<DeskflowSwitcherItem *> *items = [NSMutableArray arrayWithCapacity:order.size()];
  for (const auto index : order) {
    [items addObject:found[index]];
  }
  _items = items;
  if (_items.count == 0) {
    LOG_DEBUG("window switcher: no windows to switch to");
    return;
  }

  _open = YES;
  _selection = deskflow::osx::switcherInitialSelection(_items.count, reverse);
  [_panel showItems:_items selection:_selection];
  LOG_DEBUG(
      "window switcher: opened with %lu windows in %.0f ms", static_cast<unsigned long>(_items.count),
      (CFAbsoluteTimeGetCurrent() - started) * 1000.0
  );
  [self armIdleTimeout];
}

- (void)armIdleTimeout
{
  const uint64_t generation = ++_generation;
  __weak DeskflowWindowSwitcherController *weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kIdleTimeoutSeconds * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    DeskflowWindowSwitcherController *strongSelf = weakSelf;
    if (strongSelf != nil && strongSelf->_open && strongSelf->_generation == generation) {
      LOG_DEBUG("window switcher: closed after being idle");
      [strongSelf cancel];
    }
  });
}

- (void)step:(int)delta
{
  if (!_open || _items.count == 0) {
    return;
  }
  _selection = deskflow::osx::switcherStep(_selection, _items.count, delta);
  [_panel selectIndex:_selection];
  [self armIdleTimeout];
}

- (void)commit
{
  if (!_open || _items.count == 0) {
    [self hidePanel];
    return;
  }
  DeskflowSwitcherItem *item = _items[MIN(_selection, _items.count - 1)];
  [self hidePanel];
  [self activate:item];
}

- (void)cancel
{
  [self hidePanel];
}

- (void)hidePanel
{
  _open = NO;
  ++_generation;
  [_panel hide];
  _items = @[];
  _selection = 0;
}

- (void)activate:(DeskflowSwitcherItem *)item
{
  NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:item.pid];
  if (app == nil || app.terminated) {
    return;
  }
  auto window = (__bridge AXUIElementRef)item.axWindow;
  if (app.hidden) {
    [app unhide];
  }
  if (window != nullptr && item.minimized) {
    AXUIElementSetAttributeValue(window, kAXMinimizedAttribute, kCFBooleanFalse);
  }
  // Accessibility brings the app forward even though this background process is not active
  if (AXUIElementRef axApp = createAppElement(item.pid); axApp != nullptr) {
    AXUIElementSetAttributeValue(axApp, kAXFrontmostAttribute, kCFBooleanTrue);
    if (window != nullptr) {
      AXUIElementSetAttributeValue(axApp, kAXFocusedWindowAttribute, window);
    }
    CFRelease(axApp);
  }
  if (window != nullptr) {
    AXUIElementSetAttributeValue(window, kAXMainAttribute, kCFBooleanTrue);
    AXUIElementPerformAction(window, kAXRaiseAction);
  }
  [app activateWithOptions:0];
  _mru.touchWindow(item.pid, item.windowId);
}

@end

//! Owns the controller; shared by the switcher and its queued blocks, touched on the main thread only.
@interface DeskflowWindowSwitcherBox : NSObject
@property(nonatomic, strong) DeskflowWindowSwitcherController *controller;
@end

@implementation DeskflowWindowSwitcherBox
@end

namespace deskflow::osx {

struct OSXWindowSwitcher::Impl
{
  DeskflowWindowSwitcherBox *box = [DeskflowWindowSwitcherBox new];
};

OSXWindowSwitcher::OSXWindowSwitcher() : m_impl(std::make_unique<Impl>())
{
  // start tracking app activations (MRU order) right away, on the main thread
  DeskflowWindowSwitcherBox *box = m_impl->box;
  dispatch_async(dispatch_get_main_queue(), ^{
    @autoreleasepool {
      box.controller = [DeskflowWindowSwitcherController new];
    }
  });
}

OSXWindowSwitcher::~OSXWindowSwitcher()
{
  // runs after every command queued before, as the main queue is serial
  DeskflowWindowSwitcherBox *box = m_impl->box;
  dispatch_async(dispatch_get_main_queue(), ^{
    [box.controller shutdown];
    box.controller = nil;
  });
}

void OSXWindowSwitcher::handle(SwitcherCommand command)
{
  if (command == SwitcherCommand::None) {
    return;
  }
  DeskflowWindowSwitcherBox *box = m_impl->box;
  dispatch_async(dispatch_get_main_queue(), ^{
    @autoreleasepool {
      [box.controller handle:command];
    }
  });
}

} // namespace deskflow::osx

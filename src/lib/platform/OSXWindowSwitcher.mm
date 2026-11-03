/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "platform/OSXWindowSwitcher.h"

#import "base/Log.h"
#import "common/I18N.h"
#import "platform/OSXWindowSwitcherModel.h"
#import "platform/OSXWindowSwitcherPanel.h"
#import "platform/OSXWindowSwitcherSpaces.h"
#import "platform/OSXWindowSwitcherThumbnails.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>
#import <dlfcn.h>
#import <QCoreApplication>
#import <unistd.h>

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcher.mm must be compiled with ARC (-fobjc-arc)"
#endif

using deskflow::osx::SwitcherCommand;
using deskflow::osx::SwitcherMru;
using deskflow::osx::SwitcherWindowId;

namespace {

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

} // namespace

//! Main thread only.
@interface DeskflowWindowSwitcherController : NSObject
- (void)handle:(SwitcherCommand)command;
- (void)shutdown;
@end

@implementation DeskflowWindowSwitcherController {
  SwitcherMru _mru;
  DeskflowSwitcherPanel *_panel;
  DeskflowSwitcherThumbnailer *_thumbnailer;
  DeskflowSwitcherRemoteWindows *_remote;
  NSArray<DeskflowSwitcherItem *> *_items;
  NSUInteger _selection;
  BOOL _open;
  uint64_t _generation;
  NSMutableArray<id> *_observers;
  BOOL _warnedUntrusted;
  std::vector<std::vector<deskflow::osx::SwitcherTile>> _stripTiles; //!< windows of each strip item (fullscreen)
  NSDictionary<NSNumber *, NSString *> *_tileTitles;                 //!< known titles of those windows
  uint64_t _spaceChange;
}

- (instancetype)init
{
  if ((self = [super init])) {
    _panel = [DeskflowSwitcherPanel new];
    _thumbnailer = [DeskflowSwitcherThumbnailer new];
    _remote = [DeskflowSwitcherRemoteWindows new];
    // resolve the private calls early, off the main thread (the first lookup takes ~100 ms)
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
      deskflow::osx::switcherSpacesAvailable();
      [DeskflowSwitcherRemoteWindows available];
    });
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
  [_observers addObject:[center addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                                            object:nil
                                             queue:NSOperationQueue.mainQueue
                                        usingBlock:^(NSNotification *) {
                                          [weakSelf noteSpaceChanged];
                                        }]];
  [self noteActivated:NSWorkspace.sharedWorkspace.frontmostApplication];
}

//! Another Space is shown: its focused window is the most recent one now (no app activation when both
//! Spaces belong to the same app); a fullscreen window is captured while it can be, for the strip.
- (void)noteSpaceChanged
{
  [self noteActivated:NSWorkspace.sharedWorkspace.frontmostApplication];
  if (_open || ![DeskflowSwitcherThumbnailer available]) {
    return;
  }
  constexpr int64_t kSettleMs = 700; // after the Space animation
  const uint64_t change = ++_spaceChange;
  __weak DeskflowWindowSwitcherController *weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kSettleMs * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    DeskflowWindowSwitcherController *strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf->_spaceChange != change || strongSelf->_open) {
      return;
    }
    std::vector<uint32_t> windows;
    for (const auto &tile : deskflow::osx::switcherVisibleFullscreenWindows()) {
      windows.push_back(tile.windowId);
    }
    [strongSelf->_thumbnailer rememberWindows:windows];
  });
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

//! Strip items for \p strip (Spaces of the display \p screen), titled like Mission Control.
- (NSArray<DeskflowSwitcherItem *> *)stripItemsFrom:(const std::vector<deskflow::osx::SwitcherStripItem> &)strip
                                             screen:(NSScreen *)screen
                                          displayId:(uint32_t)displayId
{
  NSMutableArray<DeskflowSwitcherItem *> *items = [NSMutableArray array];
  _stripTiles.clear();
  if (strip.empty()) {
    return items;
  }
  if (QCoreApplication::instance() != nullptr) {
    I18N::instance(); // installs the translators in this process (the core has none otherwise)
  }
  NSString *currentLabel = @(deskflow::osx::switcherCurrentSpaceLabel().c_str());
  NSURL *wallpaper = screen != nil ? [NSWorkspace.sharedWorkspace desktopImageURLForScreen:screen] : nil;
  for (const auto &space : strip) {
    DeskflowSwitcherItem *item = [DeskflowSwitcherItem new];
    item.space = YES;
    item.currentSpace = space.current;
    item.badge = space.current ? currentLabel : nil;
    item.spaceId = space.spaceId;
    item.spaceDisplay = displayId;
    if (space.kind == deskflow::osx::SwitcherSpaceKind::Desktop) {
      item.desktop = YES;
      item.title = @(deskflow::osx::switcherDesktopTitle(space.number).c_str());
      item.wallpaperURL = wallpaper;
    } else if (!space.tiles.empty()) {
      const auto &front = space.tiles.front();
      NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:front.pid];
      item.pid = front.pid;
      item.windowId = front.windowId;
      item.icon = app.icon;
    }
    _stripTiles.push_back(space.tiles);
    [items addObject:item];
  }
  return items;
}

//! Titles of the fullscreen strip items from the app names and the window titles known so far.
- (void)updateStripTitles
{
  std::vector<std::vector<deskflow::osx::SwitcherTileName>> names;
  std::vector<NSUInteger> indices;
  for (NSUInteger i = 0; i < _stripTiles.size() && i < _items.count; ++i) {
    if (_stripTiles[i].empty()) {
      continue;
    }
    std::vector<deskflow::osx::SwitcherTileName> tiles;
    for (const auto &tile : _stripTiles[i]) {
      NSString *appName = [NSRunningApplication runningApplicationWithProcessIdentifier:tile.pid].localizedName ?: @"";
      NSString *title = _tileTitles[@(tile.windowId)];
      if (title.length == 0) {
        title = [_remote cachedWindow:tile.windowId].title ?: @"";
      }
      tiles.push_back({tile.pid, std::string(appName.UTF8String ?: ""), std::string(title.UTF8String ?: "")});
    }
    names.push_back(std::move(tiles));
    indices.push_back(i);
  }
  const auto titles = deskflow::osx::switcherFullscreenTitles(names);
  for (size_t k = 0; k < titles.size(); ++k) {
    NSString *title = @(titles[k].c_str()) ?: @"";
    DeskflowSwitcherItem *item = _items[indices[k]];
    if (![item.title isEqualToString:title]) {
      item.title = title;
      if (_open) {
        [_panel refreshItemAtIndex:indices[k]];
      }
    }
  }
}

//! Looks up the titles of the fullscreen windows in the background; also warms the element cache direct switching uses.
- (void)resolveTileTitles
{
  std::vector<std::pair<deskflow::osx::SwitcherPid, SwitcherWindowId>> wanted;
  for (const auto &tiles : _stripTiles) {
    for (const auto &tile : tiles) {
      if (_tileTitles[@(tile.windowId)].length == 0) {
        wanted.emplace_back(tile.pid, tile.windowId);
      }
    }
  }
  if (wanted.empty()) {
    return;
  }
  __weak DeskflowWindowSwitcherController *weakSelf = self;
  [_remote resolve:wanted
             found:^(uint32_t, DeskflowSwitcherRemoteWindow *) {
               DeskflowWindowSwitcherController *strongSelf = weakSelf;
               if (strongSelf != nil && strongSelf->_open) {
                 [strongSelf updateStripTitles];
               }
             }];
}

- (void)open:(BOOL)reverse
{
  [self hidePanel];
  if (!AXIsProcessTrusted()) {
    if (!_warnedUntrusted) {
      LOG_WARN("window switcher: accessibility permission missing, cannot switch Spaces");
      _warnedUntrusted = YES;
    }
    return;
  }
  [self ensureApplication];

  const auto started = CFAbsoluteTimeGetCurrent();
  // the frontmost window may have changed without an app activation (e.g. Command+`)
  if (NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
      front != nil && front.processIdentifier != getpid()) {
    _mru.touchWindow(front.processIdentifier, [self focusedWindowIdOf:front.processIdentifier]);
  }

  // Spaces of the display under the mouse pointer (Control+Arrow switches that one; the panel shows there)
  NSScreen *screen = [DeskflowSwitcherPanel screenWithMouse];
  NSNumber *displayId = screen.deviceDescription[@"NSScreenNumber"];
  const uint32_t spacesDisplay =
      [displayId isKindOfClass:[NSNumber class]] ? displayId.unsignedIntValue : CGMainDisplayID();
  const auto snapshot = deskflow::osx::switcherSpacesSnapshot(spacesDisplay);
  NSMutableDictionary<NSNumber *, NSString *> *tileTitles = [NSMutableDictionary dictionary];
  for (const auto &[windowId, title] : snapshot.tileTitles) {
    tileTitles[@(windowId)] = @(title.c_str());
  }
  _tileTitles = tileTitles;

  std::vector<SwitcherWindowId> alive;
  for (const auto &space : snapshot.strip) {
    for (const auto &tile : space.tiles) {
      alive.push_back(tile.windowId);
    }
  }
  _mru.retainWindows(alive);
  [_remote retainWindows:alive];

  _items = [self stripItemsFrom:snapshot.strip screen:screen displayId:spacesDisplay];
  if (_items.count == 0) {
    LOG_DEBUG("window switcher: no Spaces to switch to");
    return;
  }
  [self updateStripTitles];

  std::vector<deskflow::osx::SwitcherSelectable> selectables;
  for (NSUInteger i = 0; i < _items.count; ++i) {
    deskflow::osx::SwitcherSelectable selectable;
    selectable.window = false;
    selectable.current = _items[i].currentSpace;
    for (const auto &tile : i < _stripTiles.size() ? _stripTiles[i] : std::vector<deskflow::osx::SwitcherTile>{}) {
      if (const auto rank = _mru.windowRank(tile.windowId); rank && (!selectable.rank || *rank < *selectable.rank)) {
        selectable.rank = rank;
      }
    }
    selectables.push_back(selectable);
  }

  _open = YES;
  _selection = deskflow::osx::switcherInitialSelection(selectables, reverse);
  // thumbnails only with the Screen Recording permission (checked, never requested here);
  // the panel shows at once with cached previews or icons, fresh previews fill in as they arrive.
  // Wallpapers need no permission.
  const BOOL thumbnails = [DeskflowSwitcherThumbnailer available];
  if (thumbnails) {
    [_thumbnailer applyCachedThumbnailsTo:_items];
  }
  [self applyWallpapers];
  [_panel showItems:_items selection:_selection];
  [self loadWallpapers];
  [self resolveTileTitles];
  LOG_DEBUG(
      "window switcher: opened with %lu Spaces in %.0f ms%s", static_cast<unsigned long>(snapshot.strip.size()),
      (CFAbsoluteTimeGetCurrent() - started) * 1000.0, thumbnails ? ", with thumbnails" : ""
  );
  if (thumbnails) {
    DeskflowSwitcherPanel *panel = _panel;
    [_thumbnailer startForItems:_items
                      selection:_selection
                          scale:_panel.backingScale
                        updated:^(NSUInteger index) {
                          [panel refreshItemAtIndex:index];
                        }];
  }
  [self armIdleTimeout];
}

//! Sets the cached wallpaper as the thumbnail of every desktop item.
- (void)applyWallpapers
{
  for (DeskflowSwitcherItem *item in _items) {
    if (item.desktop && item.wallpaperURL != nil) {
      item.thumbnail = [_thumbnailer cachedWallpaper:item.wallpaperURL scale:_panel.backingScale];
    }
  }
}

//! Loads the wallpapers that are not cached yet in the background.
- (void)loadWallpapers
{
  __weak DeskflowWindowSwitcherController *weakSelf = self;
  for (DeskflowSwitcherItem *item in _items) {
    if (!item.desktop || item.wallpaperURL == nil || item.thumbnail != nil) {
      continue;
    }
    __weak DeskflowSwitcherItem *weakItem = item;
    [_thumbnailer loadWallpaper:item.wallpaperURL
                          scale:_panel.backingScale
                           done:^(NSImage *image) {
                             DeskflowWindowSwitcherController *strongSelf = weakSelf;
                             DeskflowSwitcherItem *strongItem = weakItem;
                             if (strongSelf == nil || strongItem == nil || image == nil || !strongSelf->_open) {
                               return;
                             }
                             const NSUInteger index = [strongSelf->_items indexOfObjectIdenticalTo:strongItem];
                             if (index != NSNotFound) {
                               strongItem.thumbnail = image;
                               [strongSelf->_panel refreshItemAtIndex:index];
                             }
                           }];
  }
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
    // one commit per open: a second Return / Alt release (or a late duplicate) does nothing
    LOG_DEBUG("window switcher: commit ignored, %s", _open ? "nothing to select" : "not open");
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
  const BOOL wasOpen = _open;
  _open = NO;
  ++_generation;
  if (wasOpen) {
    [_thumbnailer cancel];
  }
  [_panel hide];
  _items = @[];
  _selection = 0;
  _stripTiles.clear();
  _tileTitles = nil;
}

- (void)activate:(DeskflowSwitcherItem *)item
{
  if (item.space && !item.currentSpace) {
    deskflow::osx::switcherSwitchToSpace(item.spaceDisplay, item.spaceId, _remote);
  }
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

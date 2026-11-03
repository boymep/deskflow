/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#import "platform/OSXWindowSwitcherThumbnails.h"

#import "base/Log.h"
#import "platform/OSXWindowSwitcherModel.h"

#import <ApplicationServices/ApplicationServices.h>
#import <dispatch/dispatch.h>

#include <deque>
#include <memory>
#include <vector>

// ScreenCaptureKit screenshots need the macOS 14 SDK; older SDKs build without thumbnails
#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>) && defined(__MAC_14_0)
#define DESKFLOW_SWITCHER_SCK 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#else
#define DESKFLOW_SWITCHER_SCK 0
#endif

#if !__has_feature(objc_arc)
#error "OSXWindowSwitcherThumbnails.mm must be compiled with ARC (-fobjc-arc)"
#endif

using deskflow::osx::SwitcherCaptureWindow;
using deskflow::osx::SwitcherLru;
using deskflow::osx::SwitcherRect;
using deskflow::osx::SwitcherWindowId;

namespace {

constexpr size_t kMaxCaptures = 30;          // per open, in display order (selection first)
constexpr int kMaxConcurrentCaptures = 3;    // screenshots in flight at once
constexpr int kMaxPixelWidth = 400;          // per thumbnail
constexpr size_t kCacheEntries = 40;         // last thumbnails kept (about 400 KB each at most)
constexpr int64_t kCacheLifetimeSeconds = 300; // cache dropped this long after the switcher closes

SwitcherRect rectOf(CGRect r)
{
  return {r.origin.x, r.origin.y, r.size.width, r.size.height};
}

} // namespace

#if DESKFLOW_SWITCHER_SCK
//! One pending screenshot.
struct DeskflowCaptureJob
{
  NSUInteger index = 0;
  SwitcherWindowId windowId = 0;
  SCWindow *window = nil;
};
#endif

@implementation DeskflowSwitcherThumbnailer {
  std::unique_ptr<SwitcherLru<SwitcherWindowId, NSImage *>> _cache;
  NSArray<DeskflowSwitcherItem *> *_items;
  void (^_updated)(NSUInteger);
  CGFloat _scale;
  NSUInteger _selection;
  uint64_t _generation;
  uint64_t _cacheGeneration;
  int _inFlight;
#if DESKFLOW_SWITCHER_SCK
  std::deque<DeskflowCaptureJob> _pending;
#endif
}

+ (BOOL)available
{
#if DESKFLOW_SWITCHER_SCK
  if (@available(macOS 14.0, *)) {
    // checks only, unlike CGRequestScreenCaptureAccess this never shows a prompt
    return CGPreflightScreenCaptureAccess();
  }
#endif
  return NO;
}

- (instancetype)init
{
  if ((self = [super init])) {
    _cache = std::make_unique<SwitcherLru<SwitcherWindowId, NSImage *>>(kCacheEntries);
    _items = @[];
  }
  return self;
}

- (void)applyCachedThumbnailsTo:(NSArray<DeskflowSwitcherItem *> *)items
{
  for (DeskflowSwitcherItem *item in items) {
    if (item.windowId == 0) {
      continue;
    }
    if (NSImage *const *cached = _cache->find(item.windowId); cached != nullptr) {
      item.thumbnail = *cached;
    }
  }
}

- (void)startForItems:(NSArray<DeskflowSwitcherItem *> *)items
            selection:(NSUInteger)selection
                scale:(CGFloat)scale
              updated:(void (^)(NSUInteger index))updated
{
  [self dropPending];
  ++_cacheGeneration; // keep the cache while the switcher is open
  _items = [items copy];
  _selection = selection;
  _scale = scale > 0 ? scale : 2.0;
  _updated = [updated copy];
  if (_items.count == 0 || ![DeskflowSwitcherThumbnailer available]) {
    return;
  }

#if DESKFLOW_SWITCHER_SCK
  if (@available(macOS 14.0, *)) {
    const uint64_t generation = _generation;
    __weak DeskflowSwitcherThumbnailer *weakSelf = self;
    // off-screen windows too: other Spaces may still be capturable
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:NO
                                                 completionHandler:^(SCShareableContent *content, NSError *error) {
                                                   const NSInteger code = error != nil ? error.code : 0;
                                                   dispatch_async(dispatch_get_main_queue(), ^{
                                                     [weakSelf contentArrived:content
                                                                    errorCode:code
                                                                   generation:generation];
                                                   });
                                                 }];
  }
#endif
}

- (void)cancel
{
  [self dropPending];
  const uint64_t cacheGeneration = ++_cacheGeneration;
  __weak DeskflowSwitcherThumbnailer *weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kCacheLifetimeSeconds * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
    DeskflowSwitcherThumbnailer *strongSelf = weakSelf;
    if (strongSelf != nil && strongSelf->_cacheGeneration == cacheGeneration) {
      strongSelf->_cache->clear();
    }
  });
}

- (void)dropPending
{
  ++_generation;
#if DESKFLOW_SWITCHER_SCK
  _pending.clear();
#endif
  _items = @[];
  _updated = nil;
}

#if DESKFLOW_SWITCHER_SCK

- (void)contentArrived:(SCShareableContent *)content
             errorCode:(NSInteger)errorCode
            generation:(uint64_t)generation API_AVAILABLE(macos(14.0))
{
  if (generation != _generation) {
    return;
  }
  if (content == nil) {
    LOG_DEBUG("window switcher: no window list for thumbnails, error %ld", static_cast<long>(errorCode));
    return;
  }

  NSArray<SCWindow *> *scWindows = content.windows;
  std::vector<SwitcherCaptureWindow> windows;
  windows.reserve(scWindows.count);
  for (SCWindow *window in scWindows) {
    windows.push_back(SwitcherCaptureWindow{
        window.windowID, window.owningApplication != nil ? window.owningApplication.processID : 0,
        window.title != nil ? std::string(window.title.UTF8String ?: "") : std::string(), rectOf(window.frame)
    });
  }

  std::vector<SwitcherWindowId> claimed;
  for (const auto index : deskflow::osx::switcherCaptureOrder(_items.count, _selection, kMaxCaptures)) {
    DeskflowSwitcherItem *item = _items[index];
    if (item.minimized) {
      continue; // a minimized window has no current content; keep the cached preview
    }
    const SwitcherCaptureWindow query{
        item.windowId, item.pid, std::string(item.title.UTF8String ?: ""), rectOf(item.frame)
    };
    const auto match = deskflow::osx::matchSwitcherWindow(query, windows, claimed);
    if (!match.has_value()) {
      continue;
    }
    claimed.push_back(windows[*match].windowId);
    _pending.push_back(DeskflowCaptureJob{index, windows[*match].windowId, scWindows[*match]});
  }
  LOG_DEBUG(
      "window switcher: capturing %lu of %lu thumbnails", static_cast<unsigned long>(_pending.size()),
      static_cast<unsigned long>(_items.count)
  );
  [self pump];
}

- (void)pump API_AVAILABLE(macos(14.0))
{
  while (_inFlight < kMaxConcurrentCaptures && !_pending.empty()) {
    const DeskflowCaptureJob job = _pending.front();
    _pending.pop_front();
    [self capture:job];
  }
}

- (void)capture:(const DeskflowCaptureJob &)job API_AVAILABLE(macos(14.0))
{
  SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:job.window];
  CGRect content = filter.contentRect;
  if (CGRectIsEmpty(content)) {
    content = job.window.frame;
  }
  const NSSize box = [DeskflowSwitcherPanel thumbnailBoxSize];
  const auto size =
      deskflow::osx::switcherThumbnailSize(rectOf(content), {0, 0, box.width, box.height}, _scale, kMaxPixelWidth);
  if (size.width == 0 || size.height == 0) {
    return;
  }

  SCStreamConfiguration *config = [SCStreamConfiguration new];
  config.width = static_cast<size_t>(size.width);
  config.height = static_cast<size_t>(size.height);
  config.scalesToFit = YES;
  config.preservesAspectRatio = YES;
  config.showsCursor = NO;
  config.ignoreShadowsSingleWindow = YES;
  config.capturesAudio = NO;

  ++_inFlight;
  const uint64_t generation = _generation;
  const NSUInteger index = job.index;
  const SwitcherWindowId windowId = job.windowId;
  const NSSize points = NSMakeSize(size.width / _scale, size.height / _scale);
  __weak DeskflowSwitcherThumbnailer *weakSelf = self;
  [SCScreenshotManager
      captureImageWithFilter:filter
               configuration:config
           completionHandler:^(CGImageRef image, NSError *error) {
             // the image is only valid during this call: wrap it before hopping to the main thread
             NSImage *thumbnail = image != nullptr ? [[NSImage alloc] initWithCGImage:image size:points] : nil;
             const NSInteger code = error != nil ? error.code : 0;
             dispatch_async(dispatch_get_main_queue(), ^{
               [weakSelf captured:thumbnail
                            index:index
                         windowId:windowId
                        errorCode:code
                       generation:generation];
             });
           }];
}

- (void)captured:(NSImage *)thumbnail
           index:(NSUInteger)index
        windowId:(SwitcherWindowId)windowId
       errorCode:(NSInteger)errorCode
      generation:(uint64_t)generation API_AVAILABLE(macos(14.0))
{
  --_inFlight;
  if (thumbnail != nil) {
    _cache->put(windowId, thumbnail);
  } else {
    LOG_VERBOSE("window switcher: thumbnail capture failed, error %ld", static_cast<long>(errorCode));
  }
  // pending jobs always belong to the current run, so a slot freed by an older capture is used too
  [self pump];
  if (generation != _generation) {
    return; // the switcher closed or reopened meanwhile
  }
  if (thumbnail != nil && index < _items.count) {
    _items[index].thumbnail = thumbnail;
    if (_updated != nil) {
      _updated(index);
    }
  }
}

#endif

@end

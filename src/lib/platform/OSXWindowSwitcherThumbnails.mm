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

#include <algorithm>
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
constexpr size_t kSpaceCacheEntries = 16;    // fullscreen Space windows, kept while they exist

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
  std::unique_ptr<SwitcherLru<SwitcherWindowId, NSImage *>> _spaceCache;
  NSArray<DeskflowSwitcherItem *> *_items;
  void (^_updated)(NSUInteger);
  CGFloat _scale;
  NSUInteger _selection;
  uint64_t _generation;
  uint64_t _cacheGeneration;
  int _inFlight;
  NSMutableDictionary<NSString *, NSImage *> *_wallpapers; //!< by file URL and scale
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
    _spaceCache = std::make_unique<SwitcherLru<SwitcherWindowId, NSImage *>>(kSpaceCacheEntries);
    _items = @[];
    _wallpapers = [NSMutableDictionary dictionary];
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
    } else if (NSImage *const *kept = _spaceCache->find(item.windowId); kept != nullptr) {
      item.thumbnail = *kept;
    }
  }
}

static NSString *wallpaperKey(NSURL *url, CGFloat scale)
{
  return [NSString stringWithFormat:@"%@@%g", url.absoluteString, scale];
}

- (NSImage *)cachedWallpaper:(NSURL *)url scale:(CGFloat)scale
{
  return _wallpapers[wallpaperKey(url, scale > 0 ? scale : 2.0)];
}

- (void)loadWallpaper:(NSURL *)url scale:(CGFloat)scale done:(void (^)(NSImage *_Nullable))done
{
  if (scale <= 0) {
    scale = 2.0;
  }
  NSString *key = wallpaperKey(url, scale);
  if (NSImage *cached = _wallpapers[key]; cached != nil) {
    done(cached);
    return;
  }
  const NSSize box = [DeskflowSwitcherPanel thumbnailBoxSize];
  const CGFloat maxPixels = MAX(box.width, box.height) * scale;
  __weak DeskflowSwitcherThumbnailer *weakSelf = self;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    CGImageRef image = nullptr;
    if (CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr); source != nullptr) {
      NSDictionary *options = @{
        (__bridge NSString *)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
        (__bridge NSString *)kCGImageSourceCreateThumbnailWithTransform : @YES,
        (__bridge NSString *)kCGImageSourceThumbnailMaxPixelSize : @(maxPixels)
      };
      image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
      CFRelease(source);
    }
    NSImage *result = nil;
    if (image != nullptr) {
      result = [[NSImage alloc] initWithCGImage:image
                                           size:NSMakeSize(CGImageGetWidth(image) / scale, CGImageGetHeight(image) / scale)];
      CGImageRelease(image);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      DeskflowSwitcherThumbnailer *strongSelf = weakSelf;
      if (result != nil && strongSelf != nil) {
        if (strongSelf->_wallpapers.count >= 8) {
          [strongSelf->_wallpapers removeAllObjects];
        }
        strongSelf->_wallpapers[key] = result;
      }
      done(result);
    });
  });
}

- (void)rememberWindows:(const std::vector<uint32_t> &)windowIds
{
  if (windowIds.empty() || ![DeskflowSwitcherThumbnailer available]) {
    return;
  }
#if DESKFLOW_SWITCHER_SCK
  if (@available(macOS 14.0, *)) {
    const std::vector<uint32_t> wanted = windowIds;
    const CGFloat scale = _scale > 0 ? _scale : 2.0;
    __weak DeskflowSwitcherThumbnailer *weakSelf = self;
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent *content, NSError *) {
                                                   for (SCWindow *window in content.windows) {
                                                     if (std::find(wanted.begin(), wanted.end(), window.windowID) !=
                                                         wanted.end()) {
                                                       [weakSelf rememberWindow:window scale:scale];
                                                     }
                                                   }
                                                 }];
  }
#endif
}

#if DESKFLOW_SWITCHER_SCK
//! Any thread. Captures \p window into the Space cache.
- (void)rememberWindow:(SCWindow *)window scale:(CGFloat)scale API_AVAILABLE(macos(14.0))
{
  SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
  CGRect content = filter.contentRect;
  if (CGRectIsEmpty(content)) {
    content = window.frame;
  }
  const NSSize box = [DeskflowSwitcherPanel thumbnailBoxSize];
  const auto size =
      deskflow::osx::switcherThumbnailSize(rectOf(content), {0, 0, box.width, box.height}, scale, kMaxPixelWidth);
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
  const SwitcherWindowId windowId = window.windowID;
  const NSSize points = NSMakeSize(size.width / scale, size.height / scale);
  __weak DeskflowSwitcherThumbnailer *weakSelf = self;
  [SCScreenshotManager captureImageWithFilter:filter
                                configuration:config
                            completionHandler:^(CGImageRef image, NSError *) {
                              NSImage *thumbnail =
                                  image != nullptr ? [[NSImage alloc] initWithCGImage:image size:points] : nil;
                              if (thumbnail == nil) {
                                return;
                              }
                              dispatch_async(dispatch_get_main_queue(), ^{
                                DeskflowSwitcherThumbnailer *strongSelf = weakSelf;
                                if (strongSelf != nil) {
                                  strongSelf->_spaceCache->put(windowId, thumbnail);
                                }
                              });
                            }];
}
#endif

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
      [strongSelf->_wallpapers removeAllObjects];
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
    if (item.desktop) {
      continue; // a desktop has its wallpaper
    }
    const SwitcherCaptureWindow query{
        item.windowId, item.pid, std::string(item.title.UTF8String ?: ""), rectOf(CGRectZero)
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
    if (_items[index].space) {
      _spaceCache->put(windowId, thumbnail);
    }
    _items[index].thumbnail = thumbnail;
    if (_updated != nil) {
      _updated(index);
    }
  }
}

#endif

@end

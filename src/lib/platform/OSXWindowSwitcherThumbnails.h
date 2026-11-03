/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#import "platform/OSXWindowSwitcherPanel.h"

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

//! Captures window thumbnails for the built-in window switcher with ScreenCaptureKit.
/*!
Main thread only. Needs macOS 14 and the Screen Recording permission of the
Deskflow app (granted from the client settings; it is never requested here, so
opening the switcher cannot show a system prompt). Without either, nothing is
captured and the switcher shows app icons only.

Thumbnails live in memory only: they are never written to disk or logged. The
last image per window is kept in a small cache so minimized windows and
windows that cannot be captured right now still show their last preview; the
cache is dropped a few minutes after the switcher closes.
*/
@interface DeskflowSwitcherThumbnailer : NSObject
//! True if thumbnails can be captured now (OS, SDK and permission; never prompts).
+ (BOOL)available;
//! Sets the cached thumbnail of every item that has one.
- (void)applyCachedThumbnailsTo:(NSArray<DeskflowSwitcherItem *> *)items;
//! Captures thumbnails for \p items asynchronously, \p selection first; \p updated runs on the main thread
//! with the index of each item whose thumbnail was set. Cancels a previous run.
- (void)startForItems:(NSArray<DeskflowSwitcherItem *> *)items
            selection:(NSUInteger)selection
                scale:(CGFloat)scale
              updated:(void (^)(NSUInteger index))updated;
//! Drops pending captures (running ones are ignored when they finish) and schedules the cache cleanup.
- (void)cancel;
@end

NS_ASSUME_NONNULL_END

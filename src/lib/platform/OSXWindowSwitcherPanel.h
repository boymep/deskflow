/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

//! One window shown by the built-in window switcher.
@interface DeskflowSwitcherItem : NSObject
@property(nonatomic, copy) NSString *title;   //!< window title, may be empty
@property(nonatomic, copy) NSString *appName; //!< shown when the title is empty
@property(nonatomic, strong, nullable) NSImage *icon;
@property(nonatomic, assign) BOOL minimized;
@property(nonatomic, assign) BOOL appHidden;
//! Window preview; not shown yet (reserved for thumbnails).
@property(nonatomic, strong, nullable) NSImage *thumbnail;
// used by the controller only
@property(nonatomic, assign) pid_t pid;
@property(nonatomic, assign) uint32_t windowId; //!< CGWindowID, 0 if unknown
@property(nonatomic, strong, nullable) id axWindow; //!< AXUIElementRef of the window
@end

//! Borderless, non-activating panel that shows switcher items in a grid.
/*!
Main thread only. The panel never becomes key or main and ignores the mouse,
so it cannot take focus from the application the user is working in.
*/
@interface DeskflowSwitcherPanel : NSObject
@property(nonatomic, readonly) BOOL visible;
@property(nonatomic, readonly) NSUInteger selection;
//! Shows \p items centered on the screen with the mouse pointer.
- (void)showItems:(NSArray<DeskflowSwitcherItem *> *)items selection:(NSUInteger)selection;
- (void)selectIndex:(NSUInteger)index;
- (void)hide;
//! Builds the panel content for \p items without showing it (also used to render previews).
- (NSView *)buildContentForItems:(NSArray<DeskflowSwitcherItem *> *)items
                       selection:(NSUInteger)selection
                     screenFrame:(NSRect)screenFrame;
@end

NS_ASSUME_NONNULL_END

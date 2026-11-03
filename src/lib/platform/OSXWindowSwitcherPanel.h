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
//! Window preview, in memory only; drawn when the panel shows thumbnails.
@property(nonatomic, strong, nullable) NSImage *thumbnail;
// used by the controller only
@property(nonatomic, assign) pid_t pid;
@property(nonatomic, assign) uint32_t windowId; //!< CGWindowID, 0 if unknown
@property(nonatomic, strong, nullable) id axWindow; //!< AXUIElementRef of the window
@property(nonatomic, assign) CGRect frame;          //!< Accessibility frame, global points, top-left origin
@property(nonatomic, assign) BOOL otherSpace;       //!< on a Space that is not shown right now
@property(nonatomic, assign) BOOL space;           //!< a Space of the strip (desktop or fullscreen app), not a window
@property(nonatomic, assign) BOOL desktop;         //!< a desktop Space: its thumbnail is the wallpaper
@property(nonatomic, assign) BOOL currentSpace;    //!< the Space shown right now
@property(nonatomic, copy, nullable) NSString *badge; //!< small label on the thumbnail (e.g. "Current")
@property(nonatomic, assign) uint64_t spaceId;        //!< window server id of the Space (space items)
@property(nonatomic, assign) uint32_t spaceDisplay;   //!< CGDirectDisplayID the Space belongs to (space items)
@property(nonatomic, strong, nullable) NSURL *wallpaperURL; //!< wallpaper shown as the thumbnail of a desktop
@end

//! Borderless, non-activating panel that shows switcher items: a strip of Spaces above a grid of windows.
/*!
Items flagged as space come first and form the strip (like the top bar of Mission
Control); the windows follow in the grid. The selection moves through both in that order.

Main thread only. The panel never becomes key or main and ignores the mouse,
so it cannot take focus from the application the user is working in.
*/
@interface DeskflowSwitcherPanel : NSObject
@property(nonatomic, readonly) BOOL visible;
@property(nonatomic, readonly) NSUInteger selection;
//! Backing scale factor of the screen the panel was last shown on.
@property(nonatomic, readonly) CGFloat backingScale;
//! The screen with the mouse pointer (where the panel shows), or the main screen.
+ (nullable NSScreen *)screenWithMouse;
//! Size, in points, of the thumbnail area of one tile.
+ (NSSize)thumbnailBoxSize;
//! Shows \p items centered on the screen with the mouse pointer.
/*!
With \p thumbnails the window tiles are larger and draw each item's thumbnail with the
app icon on top (the icon alone until a thumbnail arrives); without, tiles show
only the app icon. Strip tiles always show thumbnails (wallpapers need no permission).
*/
- (void)showItems:(NSArray<DeskflowSwitcherItem *> *)items
        selection:(NSUInteger)selection
       thumbnails:(BOOL)thumbnails;
- (void)selectIndex:(NSUInteger)index;
//! Redraws the tile of item \p index (e.g. after its thumbnail or title changed).
- (void)refreshItemAtIndex:(NSUInteger)index;
- (void)hide;
//! Builds the panel content for \p items without showing it (also used to render previews).
- (NSView *)buildContentForItems:(NSArray<DeskflowSwitcherItem *> *)items
                       selection:(NSUInteger)selection
                     screenFrame:(NSRect)screenFrame
                      thumbnails:(BOOL)thumbnails;
@end

NS_ASSUME_NONNULL_END

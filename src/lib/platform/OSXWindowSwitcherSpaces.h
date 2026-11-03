/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#import "platform/OSXWindowSwitcherModel.h"

#import <Cocoa/Cocoa.h>

#include <map>
#include <string>
#include <vector>

/*
Windows on other Spaces (Mission Control desktops and fullscreen apps) for the
built-in window switcher.

Public APIs only report windows of the visible Spaces, so this uses private
window server (CGS / SkyLight) and Accessibility calls. Every one is looked up
at runtime; if any is missing, the switcher keeps its current-Space-only
behaviour. Window titles are never logged.
*/

namespace deskflow::osx {

//! A window of the switcher's window grid as listed by the window server.
struct SwitcherSpaceWindowInfo
{
  SwitcherWindowId windowId = 0;
  SwitcherPid pid = 0;
  std::string title; //!< UTF-8 window server title; empty without the Screen Recording permission
  SwitcherRect frame;
  bool visible = false; //!< on a Space shown right now
};

//! Spaces and desktop windows, as Mission Control shows them.
struct SwitcherSpacesSnapshot
{
  bool valid = false; //!< false: Spaces unknown, list the windows Accessibility reports instead
  std::vector<SwitcherStripItem> strip;          //!< Spaces of the display asked for, Mission Control order
  std::vector<SwitcherSpaceWindowInfo> windows;  //!< grid windows of all displays, front to back
  std::map<SwitcherWindowId, std::string> tileTitles; //!< window server titles of fullscreen windows, if known
};

//! True if an Accessibility window with \p role, \p subrole and \p title belongs in the switcher.
bool switcherIsSwitchableAXWindow(NSString *_Nullable role, NSString *_Nullable subrole, NSString *_Nullable title);

//! True if Spaces can be listed and windows on other Spaces activated (all private calls found). Thread safe.
bool switcherSpacesAvailable();

//! The Spaces strip of display \p displayId (CGDirectDisplayID) and the windows of the grid.
/*!
Main thread. Windows are those of \p allowed processes chosen by selectSwitcherDesktopWindows
(\p axSwitchable and \p axAnswered come from the Accessibility listing of the visible Spaces),
at most \p limit. One or two window server calls per window; invalid if switcherSpacesAvailable()
is false or a call fails.
*/
SwitcherSpacesSnapshot switcherSpacesSnapshot(
    uint32_t displayId, const std::vector<SwitcherPid> &allowed, const std::vector<SwitcherWindowId> &axSwitchable,
    const std::vector<SwitcherPid> &axAnswered, size_t limit
);

//! Windows of the fullscreen Spaces shown right now (all displays); empty if none or unknown.
std::vector<SwitcherTile> switcherVisibleFullscreenWindows();

//! Switches display \p displayId (CGDirectDisplayID) to Space \p spaceId.
/*!
Main thread, returns at once. A fullscreen Space is reached by focusing its window. Otherwise one
Control+Arrow (the same key event as the side buttons) is posted at a time; the next one only after
the window server reports the expected Space as current, and the remaining distance is recomputed
from the Space that is current then, so the switch never goes past the target. It stops if a step
does not land within a second, if anybody else switches Spaces meanwhile, or when a newer switch
starts. Each step is logged at INFO.
*/
void switcherSwitchToSpace(uint32_t displayId, uint64_t spaceId);

//! Brings window \p windowId of \p pid to the front and makes it key; macOS switches to its Space.
/*!
Main thread. Returns false (nothing done) if the private calls are missing or fail.
*/
bool switcherFocusWindow(SwitcherPid pid, SwitcherWindowId windowId);

} // namespace deskflow::osx

NS_ASSUME_NONNULL_BEGIN

//! Accessibility element of a window on another Space, found by DeskflowSwitcherRemoteWindows.
@interface DeskflowSwitcherRemoteWindow : NSObject
@property(nonatomic, strong) id element;    //!< AXUIElementRef
@property(nonatomic, copy) NSString *title; //!< may be empty
@property(nonatomic, assign) BOOL minimized;
@end

//! Finds Accessibility elements of windows on other Spaces (which Accessibility does not list).
/*!
Main thread API. The search creates elements of the app by id and matches their
window ids; it runs on a background queue, bounded in time per app and per run,
and results are cached per window id until the window disappears.
*/
@interface DeskflowSwitcherRemoteWindows : NSObject
//! True if the private calls the search needs exist.
+ (BOOL)available;
//! The cached element of \p windowId, or nil.
- (nullable DeskflowSwitcherRemoteWindow *)cachedWindow:(uint32_t)windowId;
//! True if \p windowId turned out not to be a standard window; it should not be listed.
- (BOOL)isRejected:(uint32_t)windowId;
//! Marks \p windowIds as not standard windows (Accessibility listed them on a visible Space).
- (void)reject:(const std::vector<deskflow::osx::SwitcherWindowId> &)windowIds;
//! Forgets every window not in \p alive.
- (void)retainWindows:(const std::vector<deskflow::osx::SwitcherWindowId> &)alive;
//! Searches the windows in \p wanted (pid, window id) that are not cached yet; \p found runs on the main
//! thread once per window found.
- (void)resolve:(const std::vector<std::pair<deskflow::osx::SwitcherPid, deskflow::osx::SwitcherWindowId>> &)wanted
          found:(void (^)(uint32_t windowId, DeskflowSwitcherRemoteWindow *window))found;
@end

NS_ASSUME_NONNULL_END

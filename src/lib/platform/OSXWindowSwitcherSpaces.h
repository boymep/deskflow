/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#import "platform/OSXWindowSwitcherModel.h"

#import <Cocoa/Cocoa.h>

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

//! A window on a Space that is not shown right now.
struct SwitcherSpaceWindowInfo
{
  SwitcherWindowId windowId = 0;
  SwitcherPid pid = 0;
  std::string title; //!< UTF-8 window server title; empty without the Screen Recording permission
  SwitcherRect frame;
};

//! True if an Accessibility window with \p role, \p subrole and \p title belongs in the switcher.
bool switcherIsSwitchableAXWindow(NSString *_Nullable role, NSString *_Nullable subrole, NSString *_Nullable title);

//! True if windows on other Spaces can be listed and activated (all private calls found). Thread safe.
bool switcherSpacesAvailable();

//! Windows of \p allowed processes on other Spaces that are not in \p known, front to back, at most \p limit.
/*!
Fast (a few window server calls); empty if switcherSpacesAvailable() is false
or a call fails.
*/
std::vector<SwitcherSpaceWindowInfo> switcherOtherSpaceWindows(
    const std::vector<SwitcherWindowId> &known, const std::vector<SwitcherPid> &allowed, size_t limit
);

//! Empty desktops of the display \p displayId (CGDirectDisplayID) except its current Space, in Mission Control order.
/*!
A desktop is empty if none of the plausible user windows of \p allowed processes is on it (windows on all
desktops do not count). Slow-ish (one window server call per window); empty if switcherSpacesAvailable()
is false, the display is unknown or a call fails.
*/
std::vector<SwitcherDesktop> switcherEmptyDesktopsOnDisplay(uint32_t displayId, const std::vector<SwitcherPid> &allowed);

//! Switches the display under the mouse pointer by \p steps desktops (positive: right) with Control+Arrow.
/*!
Main thread. Posts the same key events as the side buttons (Control and fn flags), one step every
few hundred milliseconds so the Space animation can finish. Does nothing for 0 steps.
*/
void switcherSwitchSpaces(int steps);

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
//! Forgets every window not in \p alive.
- (void)retainWindows:(const std::vector<deskflow::osx::SwitcherWindowId> &)alive;
//! Searches the windows in \p wanted (pid, window id) that are not cached yet; \p found runs on the main
//! thread once per window found.
- (void)resolve:(const std::vector<std::pair<deskflow::osx::SwitcherPid, deskflow::osx::SwitcherWindowId>> &)wanted
          found:(void (^)(uint32_t windowId, DeskflowSwitcherRemoteWindow *window))found;
@end

NS_ASSUME_NONNULL_END

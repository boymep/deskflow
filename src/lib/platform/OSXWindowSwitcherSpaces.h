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

@class DeskflowSwitcherRemoteWindows;

namespace deskflow::osx {

//! The Spaces strip of one display, as Mission Control shows it.
struct SwitcherSpacesSnapshot
{
  bool valid = false; //!< false: Spaces unknown, the strip is empty
  std::vector<SwitcherStripItem> strip;          //!< Spaces of the display asked for, current first, then by recent use
  std::map<SwitcherWindowId, std::string> tileTitles; //!< window server titles of fullscreen windows, if known
};

//! True if an Accessibility window with \p role, \p subrole and \p title belongs in the switcher.
bool switcherIsSwitchableAXWindow(NSString *_Nullable role, NSString *_Nullable subrole, NSString *_Nullable title);

//! True if Spaces can be listed and windows on other Spaces activated (all private calls found). Thread safe.
bool switcherSpacesAvailable();

//! The Spaces strip (recent-use order) of display \p displayId (CGDirectDisplayID) and the known titles of its fullscreen windows.
/*!
Main thread. Invalid (empty strip) if switcherSpacesAvailable() is false or a call fails.
*/
SwitcherSpacesSnapshot switcherSpacesSnapshot(uint32_t displayId);

//! Records the Space that is current on every display (for the recent-use order of the snapshot).
/*!
Main thread; call when the active Space changed. The snapshot also records the current Space.
*/
void switcherNoteActiveSpaces();

//! Windows of the fullscreen Spaces shown right now (all displays); empty if none or unknown.
std::vector<SwitcherTile> switcherVisibleFullscreenWindows();

//! Switches display \p displayId (CGDirectDisplayID) to Space \p spaceId.
/*!
Main thread, returns at once. A desktop of the first display is first jumped to by its "Switch to
Desktop N" keyboard shortcut, if that is turned on in the Keyboard settings (read on every switch,
never written; N counts the desktops only). If the current Space does not change within about 1.6
seconds the shortcut is not used again until its keys change, and the ways below are tried.

A fullscreen Space, or a desktop with a window on it, is otherwise jumped
to directly (one transition, like the Dock): the window's app is brought forward by the methods of
SwitcherDirectMethod, one after another, until the window server reports a Space transition or the
target as current (about a second at most; \p remote supplies Accessibility elements of windows on
other Spaces). The method that works is tried first next time; DESKFLOW_SWITCHER_DIRECT_ORDER (e.g.
"CAB", or "none") sets the initial order.

Otherwise (empty desktops, or every method failed) the Space is reached by stepping: one
Control+Arrow (the same key event as the side buttons) is posted at a time; the next one only after
the window server reports the expected Space as current, and the remaining distance is recomputed
from the Space that is current then, so the switch never goes past the target. It stops if a step
does not land within a second, if anybody else switches Spaces meanwhile, or when a newer switch
starts. Each method and step is logged at INFO.
*/
void switcherSwitchToSpace(uint32_t displayId, uint64_t spaceId, DeskflowSwitcherRemoteWindows *_Nullable remote);

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
//! The element of \p windowId of \p pid: cached, or searched now on the calling thread for at most \p budget seconds.
- (nullable DeskflowSwitcherRemoteWindow *)windowNow:(uint32_t)windowId
                                                 pid:(pid_t)pid
                                              budget:(CFAbsoluteTime)budget;
//! Forgets every window not in \p alive.
- (void)retainWindows:(const std::vector<deskflow::osx::SwitcherWindowId> &)alive;
//! Searches the windows in \p wanted (pid, window id) that are not cached yet; \p found runs on the main
//! thread once per window found.
- (void)resolve:(const std::vector<std::pair<deskflow::osx::SwitcherPid, deskflow::osx::SwitcherWindowId>> &)wanted
          found:(void (^)(uint32_t windowId, DeskflowSwitcherRemoteWindow *window))found;
@end

NS_ASSUME_NONNULL_END

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <functional>

void requestOSXNotificationPermission();
bool isOSXDevelopmentBuild();
bool showOSXNotification(const QString &title, const QString &body);
bool isOSXInterfaceStyleDark();
void forceAppActive();
void macOSNativeHide();
void installQuitHandler(std::function<bool()> shouldQuit);

//! True if Deskflow may capture the screen (Screen Recording permission); never prompts.
/*!
TCC records the permission for the Deskflow app bundle. deskflow-core is started
by the GUI from the same bundle, so macOS attributes its captures to Deskflow
as well: a grant made here also covers the client's window switcher thumbnails.
*/
bool hasOSXScreenCapturePermission();
//! Asks for the Screen Recording permission. macOS shows its prompt only the first time;
//! returns true if the permission is already granted.
bool requestOSXScreenCapturePermission();
//! Opens System Settings at Privacy & Security > Screen & System Audio Recording.
void openOSXScreenCaptureSettings();

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui::updates {

/**
 * @brief Starts a detached helper that installs the verified update after this app has quit
 * and then starts the app again.
 *
 * macOS: replaces the running .app bundle with the unpacked one. Windows: runs the msi with msiexec,
 * which asks for elevation (UAC).
 *
 * @param updatePath the unpacked Deskflow.app (macOS) or the verified msi (Windows).
 * @param workDir the temporary folder that holds the update. The helper removes it after the install.
 * @return true if the helper is running and the app must quit now. On false the reason is logged
 * and nothing was changed.
 */
bool startInstall(const QString &updatePath, const QString &workDir);

/**
 * @brief Shows the file in Finder or Explorer, so the user can install it by hand.
 */
void revealInFileManager(const QString &path);

} // namespace deskflow::gui::updates

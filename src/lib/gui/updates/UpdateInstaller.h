/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui::updates {

enum class InstallError
{
  None,
  NotInBundle,       ///< macOS: the app does not run from a .app bundle
  NoWriteAccess,     ///< macOS: the folder of the app is not writable for this user
  SignatureMismatch, ///< macOS: the update is not signed like the running app
  VersionMismatch,   ///< macOS: the signed bundle version is not the release or not newer
  HelperMissing,     ///< macOS: the update has no install helper
  StartFailed,       ///< the helper or msiexec could not be started
  Unsupported        ///< no automatic install on this platform
};

struct InstallStart
{
  InstallError error = InstallError::None;
  /// The update itself is trustworthy, so it can be shown for a manual install.
  bool canReveal = false;

  bool started() const
  {
    return error == InstallError::None;
  }
};

/**
 * @brief Verifies the downloaded update and starts the detached installer. On success the app must quit.
 *
 * macOS: checks the signature of the unpacked Deskflow.app against the running app and the bundle version,
 * then starts `deskflow-core --install-update` from the new bundle, which swaps the bundles after this app
 * has quit and opens the new one. Windows: starts msiexec in passive mode; the msi closes the app if it
 * still runs, may ask for elevation (UAC) and starts the app again when done.
 *
 * @param updatePath the unpacked Deskflow.app (macOS) or the verified msi (Windows).
 * @param workDir the temporary folder of the download. It is removed when the update fails the checks.
 * @param releaseVersion the version of the release that was downloaded.
 */
InstallStart startInstall(const QString &updatePath, const QString &workDir, const QString &releaseVersion);

/**
 * @brief Shows the file in Finder or Explorer, so the user can install it by hand.
 */
void revealInFileManager(const QString &path);

/**
 * @brief Removes download folders of earlier updates that are older than a day.
 */
void removeStaleWorkDirs();

} // namespace deskflow::gui::updates

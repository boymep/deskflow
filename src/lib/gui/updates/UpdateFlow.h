/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "UpdateInstaller.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class QProgressDialog;
class QWidget;

namespace deskflow::gui::updates {

class GitHubUpdater;

/**
 * @brief Drives GitHubUpdater. With automatic install on (the default) a newer release is downloaded,
 * verified and installed without questions while the status bar shows "Installing update"; then the app
 * quits and the installer starts the new version. With it off, the status bar offers the update and
 * the user confirms the install in a dialog.
 */
class UpdateFlow : public QObject
{
  Q_OBJECT
public:
  explicit UpdateFlow(QWidget *parent);

  /// Background check on startup and then every few hours. Check and download errors only go to the log.
  void checkOnStartup();
  /// Check started by the user: every result is shown.
  void checkNow();
  /// Asks whether to download and install the release found by the last check.
  void offerDownload();

Q_SIGNALS:
  /// A newer version is available and is not being installed automatically.
  void updateFound(const QString &version);
  /// A newer version is being downloaded and installed in the background.
  void installStarted(const QString &version);
  void installProgress(qint64 received, qint64 total);
  /// The background install stopped without quitting the app.
  void installStopped();
  /// The installer is running; the app must stop the core and quit now.
  void restartRequested();

private:
  void startDownload(const QString &version, bool showProgress);
  void readInstallResult();
  void showInstallError(InstallError error, const QString &updatePath, bool canReveal);
  void stopInstall(const QString &reason);
  void handleCheckFailed(const QString &message);
  void handleUpToDate(const QString &version);
  void handleUpdateAvailable(const QString &version);
  void handleProgress(qint64 received, qint64 total);
  void handleUnpacking();
  void handleDownloadFinished(const QString &updatePath);
  void handleDownloadFailed(const QString &message);
  void closeProgress();
  QWidget *dialogParent() const;

  QWidget *m_parent = nullptr;
  GitHubUpdater *m_updater = nullptr;
  QPointer<QProgressDialog> m_progress;
  bool m_interactive = false;
  bool m_cancelled = false;
  bool m_silentDownload = false;
  bool m_downloaded = false;
  bool m_installing = false;
  bool m_started = false;
  QString m_version;
  QTimer m_recheckTimer;
};

} // namespace deskflow::gui::updates

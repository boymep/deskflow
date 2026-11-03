/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class QProgressDialog;
class QWidget;

namespace deskflow::gui::updates {

class GitHubUpdater;

/**
 * @brief Dialogs around GitHubUpdater: asks before downloading, shows progress and errors,
 * then installs the update and asks the app to quit so the new version can start.
 */
class UpdateFlow : public QObject
{
  Q_OBJECT
public:
  explicit UpdateFlow(QWidget *parent);

  /// Background check on startup and then every few hours. A newer version is downloaded
  /// right away. Errors only go to the log.
  void checkOnStartup();
  /// Check started by the user: every result is shown.
  void checkNow();
  /// Asks whether to download and install the release found by the last check.
  void offerDownload();

Q_SIGNALS:
  void updateFound(const QString &version);
  /// The install helper is running; the app must quit the normal way now.
  void restartRequested();

private:
  void startDownload(const QString &version, bool showProgress);
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
  QTimer m_recheckTimer;
};

} // namespace deskflow::gui::updates

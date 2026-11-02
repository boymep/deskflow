/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QPointer>

class QProgressDialog;
class QWidget;

namespace deskflow::gui::updates {

class GitHubUpdater;

/**
 * @brief Dialogs around GitHubUpdater: asks before downloading, shows progress,
 * errors and the install steps.
 */
class UpdateFlow : public QObject
{
  Q_OBJECT
public:
  explicit UpdateFlow(QWidget *parent);

  /// Background check on startup: errors only go to the log.
  void checkOnStartup();
  /// Check started by the user: every result is shown.
  void checkNow();
  /// Asks whether to download the release found by the last check.
  void offerDownload();

Q_SIGNALS:
  void updateFound(const QString &version);

private:
  void handleNoToken();
  void handleCheckFailed(const QString &message);
  void handleUpToDate(const QString &version);
  void handleUpdateAvailable(const QString &version);
  void handleProgress(qint64 received, qint64 total);
  void handleUnpacking();
  void handleDownloadFinished(const QString &appPath);
  void handleDownloadFailed(const QString &message);
  void closeProgress();
  QWidget *dialogParent() const;

  QWidget *m_parent = nullptr;
  GitHubUpdater *m_updater = nullptr;
  QPointer<QProgressDialog> m_progress;
  bool m_interactive = false;
  bool m_cancelled = false;
};

} // namespace deskflow::gui::updates

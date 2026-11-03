/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateFlow.h"

#include "GitHubUpdater.h"
#include "UpdateInstaller.h"
#include "common/Constants.h"
#include "common/VersionInfo.h"

#include <QApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>

#include <chrono>

namespace deskflow::gui::updates {

UpdateFlow::UpdateFlow(QWidget *parent) : QObject(parent), m_parent{parent}, m_updater{new GitHubUpdater(this)}
{
  connect(m_updater, &GitHubUpdater::checkFailed, this, &UpdateFlow::handleCheckFailed);
  connect(m_updater, &GitHubUpdater::upToDate, this, &UpdateFlow::handleUpToDate);
  connect(m_updater, &GitHubUpdater::updateAvailable, this, &UpdateFlow::handleUpdateAvailable);
  connect(m_updater, &GitHubUpdater::downloadProgress, this, &UpdateFlow::handleProgress);
  connect(m_updater, &GitHubUpdater::unpacking, this, &UpdateFlow::handleUnpacking);
  connect(m_updater, &GitHubUpdater::downloadFinished, this, &UpdateFlow::handleDownloadFinished);
  connect(m_updater, &GitHubUpdater::downloadFailed, this, &UpdateFlow::handleDownloadFailed);

  constexpr auto kRecheckInterval = std::chrono::hours(6);
  m_recheckTimer.setInterval(kRecheckInterval);
  connect(&m_recheckTimer, &QTimer::timeout, this, &UpdateFlow::checkOnStartup);
}

void UpdateFlow::checkOnStartup()
{
  if (!m_recheckTimer.isActive())
    m_recheckTimer.start();
  if (m_updater->isBusy())
    return;
  m_interactive = false;
  m_updater->checkLatest();
}

void UpdateFlow::checkNow()
{
  if (m_updater->isBusy()) {
    QMessageBox::information(dialogParent(), tr("Updates"), tr("An update check or download is already running."));
    return;
  }
  m_interactive = true;
  m_updater->checkLatest();
}

void UpdateFlow::offerDownload()
{
  if (m_updater->isBusy())
    return;

  const auto version = m_updater->latestVersion();
  if (version.isEmpty()) {
    checkNow();
    return;
  }

  QMessageBox message(dialogParent());
  message.setWindowTitle(tr("Update available"));
  message.setIcon(QMessageBox::Information);
  message.setText(tr("%1 %2 is available. You have %3.").arg(kAppName, version, kVersion));
  message.setInformativeText(tr("Download and install it now? %1 will restart.").arg(kAppName));
  const auto download = message.addButton(tr("Install"), QMessageBox::AcceptRole);
  message.addButton(tr("Later"), QMessageBox::RejectRole);
  message.setDefaultButton(download);
  message.exec();
  if (message.clickedButton() != download)
    return;

  startDownload(version, true);
}

void UpdateFlow::startDownload(const QString &version, bool showProgress)
{
  m_cancelled = false;
  m_silentDownload = !showProgress;
  if (!showProgress) {
    qInfo("downloading update %s", qPrintable(version));
    m_updater->downloadLatest();
    return;
  }

  m_progress = new QProgressDialog(
      tr("Downloading %1 %2...").arg(kAppName, version), tr("Cancel"), 0, 0, dialogParent()
  );
  m_progress->setWindowTitle(tr("Update"));
  m_progress->setAttribute(Qt::WA_DeleteOnClose);
  m_progress->setMinimumDuration(0);
  m_progress->setAutoClose(false);
  m_progress->setAutoReset(false);
  connect(m_progress, &QProgressDialog::canceled, this, [this] {
    m_cancelled = true;
    m_updater->cancel();
  });
  m_progress->show();

  m_updater->downloadLatest();
}

void UpdateFlow::handleCheckFailed(const QString &message)
{
  if (!m_interactive) {
    qWarning("update check failed: %s", qPrintable(message));
    return;
  }
  QMessageBox::warning(dialogParent(), tr("Updates"), tr("Could not check for updates.\n\n%1").arg(message));
}

void UpdateFlow::handleUpToDate(const QString &version)
{
  if (!m_interactive)
    return;
  QMessageBox::information(
      dialogParent(), tr("Updates"),
      tr("You have the latest version.\n\nInstalled: %1\nLatest release: %2").arg(kVersion, version)
  );
}

void UpdateFlow::handleUpdateAvailable(const QString &version)
{
  Q_EMIT updateFound(version);
  if (m_interactive) {
    offerDownload();
    return;
  }
  if (m_downloaded || m_updater->isBusy()) {
    qDebug("update %s: already downloaded or busy, not downloading again", qPrintable(version));
    return;
  }
  startDownload(version, false);
}

void UpdateFlow::handleProgress(qint64 received, qint64 total)
{
  if (!m_progress || total <= 0)
    return;
  // progress in KiB so the range fits in an int
  m_progress->setMaximum(static_cast<int>(total / 1024));
  m_progress->setValue(static_cast<int>(qMin(received, total) / 1024));
}

void UpdateFlow::handleUnpacking()
{
  if (!m_progress)
    return;
  m_progress->setLabelText(tr("Unpacking..."));
  m_progress->setCancelButton(nullptr);
  m_progress->setRange(0, 0);
}

void UpdateFlow::handleDownloadFinished(const QString &updatePath)
{
  closeProgress();
  if (m_downloaded) {
    qDebug("update already installed in this session, ignoring: %s", qPrintable(updatePath));
    return;
  }
  m_downloaded = true;

  if (!startInstall(updatePath, m_updater->workDir())) {
    // the reason is logged by startInstall, the user installs by hand
    revealInFileManager(updatePath);
    if (m_silentDownload)
      return;
#if defined(Q_OS_WIN)
    const auto fileManager = QStringLiteral("Explorer");
#else
    const auto fileManager = QStringLiteral("Finder");
#endif
    QMessageBox::warning(
        dialogParent(), tr("Update"),
        tr("The update was downloaded but could not be installed automatically. "
           "It is shown in %1, install it by hand.")
            .arg(fileManager)
    );
    return;
  }

  if (m_silentDownload) {
    // let the signal handlers return before the window closes
    QTimer::singleShot(0, this, &UpdateFlow::restartRequested);
    return;
  }

  // short notice without buttons; it goes away with the app
  auto notice = new QMessageBox(
      QMessageBox::Information, tr("Update"), tr("Installing the update. %1 will restart.").arg(kAppName),
      QMessageBox::NoButton, dialogParent()
  );
  notice->setAttribute(Qt::WA_DeleteOnClose);
  notice->setModal(false);
  notice->show();
  constexpr auto kNoticeDelay = std::chrono::milliseconds(1500);
  QTimer::singleShot(kNoticeDelay, this, &UpdateFlow::restartRequested);
}

void UpdateFlow::handleDownloadFailed(const QString &message)
{
  closeProgress();
  if (m_cancelled) {
    m_cancelled = false;
    return;
  }
  if (m_silentDownload) {
    qWarning("automatic update download failed: %s", qPrintable(message));
    return;
  }
  QMessageBox::warning(dialogParent(), tr("Update"), tr("The update could not be downloaded.\n\n%1").arg(message));
}

void UpdateFlow::closeProgress()
{
  if (!m_progress)
    return;
  // closing a progress dialog emits canceled(), which is not a user cancel here
  m_progress->disconnect(this);
  m_progress->close();
}

QWidget *UpdateFlow::dialogParent() const
{
  // keep message boxes above the settings dialog when the check was started there
  if (auto modal = QApplication::activeModalWidget(); modal && modal != m_progress)
    return modal;
  return m_parent;
}

} // namespace deskflow::gui::updates

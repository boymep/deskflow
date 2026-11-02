/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateFlow.h"

#include "GitHubUpdater.h"
#include "UpdateTokenStore.h"
#include "common/Constants.h"
#include "common/VersionInfo.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>

namespace deskflow::gui::updates {

UpdateFlow::UpdateFlow(QWidget *parent) : QObject(parent), m_parent{parent}, m_updater{new GitHubUpdater(this)}
{
  connect(m_updater, &GitHubUpdater::noToken, this, &UpdateFlow::handleNoToken);
  connect(m_updater, &GitHubUpdater::checkFailed, this, &UpdateFlow::handleCheckFailed);
  connect(m_updater, &GitHubUpdater::upToDate, this, &UpdateFlow::handleUpToDate);
  connect(m_updater, &GitHubUpdater::updateAvailable, this, &UpdateFlow::handleUpdateAvailable);
  connect(m_updater, &GitHubUpdater::downloadProgress, this, &UpdateFlow::handleProgress);
  connect(m_updater, &GitHubUpdater::unpacking, this, &UpdateFlow::handleUnpacking);
  connect(m_updater, &GitHubUpdater::downloadFinished, this, &UpdateFlow::handleDownloadFinished);
  connect(m_updater, &GitHubUpdater::downloadFailed, this, &UpdateFlow::handleDownloadFailed);
}

void UpdateFlow::checkOnStartup()
{
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
  message.setInformativeText(tr("Download it to your Downloads folder? You install it yourself after the download."));
  const auto download = message.addButton(tr("Download"), QMessageBox::AcceptRole);
  message.addButton(tr("Later"), QMessageBox::RejectRole);
  message.setDefaultButton(download);
  message.exec();
  if (message.clickedButton() != download)
    return;

  m_cancelled = false;
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

void UpdateFlow::handleNoToken()
{
  closeProgress();
  if (!m_interactive)
    return;

  if (!UpdateTokenStore::isSupported()) {
    QMessageBox::information(dialogParent(), tr("Updates"), tr("Checking for updates is only available on macOS."));
    return;
  }

  QMessageBox::information(
      dialogParent(), tr("Updates"),
      tr("<p>To check for updates, add a GitHub token in Settings.</p>"
         "<p>Create a fine-grained token with read-only <b>Contents</b> access to the %1/%2 repository. "
         "It is stored in the macOS Keychain.</p>")
          .arg(kRepoOwner, kRepoName)
  );
}

void UpdateFlow::handleCheckFailed(const QString &message)
{
  if (!m_interactive)
    return;
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
  if (m_interactive)
    offerDownload();
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

void UpdateFlow::handleDownloadFinished(const QString &appPath)
{
  closeProgress();
  const QFileInfo info(appPath);
  QMessageBox::information(
      dialogParent(), tr("Update downloaded"),
      tr("<p>The new version is ready in <b>%1</b> and is shown in Finder.</p>"
         "<p>To install it:</p>"
         "<ol><li>Quit %2.</li>"
         "<li>Drag %3 from the Finder window to the Applications folder.</li>"
         "<li>Choose <b>Replace</b> when asked.</li>"
         "<li>Open %2 from Applications.</li></ol>")
          .arg(info.absolutePath().toHtmlEscaped(), kAppName, info.fileName().toHtmlEscaped())
  );
}

void UpdateFlow::handleDownloadFailed(const QString &message)
{
  closeProgress();
  if (m_cancelled) {
    m_cancelled = false;
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

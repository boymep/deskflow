/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateFlow.h"

#include "GitHubUpdater.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "common/UpdateInstall.h"
#include "common/VersionInfo.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <chrono>

namespace deskflow::gui::updates {

namespace {

#if defined(Q_OS_WIN)
const auto kFileManager = QStringLiteral("Explorer");
#else
const auto kFileManager = QStringLiteral("Finder");
#endif

/// System Settings, Privacy & Security, App Management.
const auto kAppManagementUrl =
    QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_AppBundles");

bool autoInstallEnabled()
{
  return Settings::value(Settings::Gui::AutoInstallUpdates).toBool();
}

/// A non-modal message box that deletes itself, so a background install never blocks the app.
QMessageBox *nonModalMessage(QWidget *parent, const QString &title, const QString &text, const QString &details)
{
  auto message = new QMessageBox(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
  message->setInformativeText(details);
  message->setAttribute(Qt::WA_DeleteOnClose);
  message->setModal(false);
  return message;
}

} // namespace

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
  if (!m_started) {
    m_started = true;
    removeStaleWorkDirs();
    readInstallResult();
  }
  if (!m_recheckTimer.isActive())
    m_recheckTimer.start();
  if (m_updater->isBusy() || m_installing)
    return;
  m_interactive = false;
  m_updater->checkLatest();
}

void UpdateFlow::checkNow()
{
  if (m_updater->isBusy() || m_installing) {
    QMessageBox::information(dialogParent(), tr("Updates"), tr("An update check or download is already running."));
    return;
  }
  m_interactive = true;
  m_updater->checkLatest();
}

void UpdateFlow::offerDownload()
{
  if (m_updater->isBusy() || m_installing)
    return;

  const auto version = m_updater->latestVersion();
  if (version.isEmpty()) {
    checkNow();
    return;
  }

  QMessageBox message(dialogParent());
  message.setWindowTitle(tr("Update"));
  message.setIcon(QMessageBox::Information);
  message.setText(tr("%1 %2 is available. You have %3.").arg(kAppName, version, kVersion));
  message.setInformativeText(tr("Download and install it now? %1 will restart.").arg(kAppName));
  const auto download = message.addButton(tr("Install"), QMessageBox::AcceptRole);
  message.addButton(tr("Later"), QMessageBox::RejectRole);
  message.setDefaultButton(download);
  message.exec();
  if (message.clickedButton() != download)
    return;

  m_interactive = true;
  startDownload(version, true);
}

void UpdateFlow::startDownload(const QString &version, bool showProgress)
{
  m_cancelled = false;
  m_installing = true;
  m_version = version;
  m_silentDownload = !showProgress;
  if (!showProgress) {
    qInfo("downloading and installing update %s", qPrintable(version));
    Q_EMIT installStarted(version);
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
  if (m_downloaded || m_installing) {
    qDebug("update %s: already installing, not downloading again", qPrintable(version));
    return;
  }

  // a version macOS refused to install is only installed again when the user asks for it
  const auto blocked = Settings::value(Settings::Gui::UpdateBlockedVersion).toString();
  if (autoInstallEnabled() && (m_interactive || version != blocked)) {
    startDownload(version, false);
    return;
  }

  Q_EMIT updateFound(version);
  if (m_interactive)
    offerDownload();
}

void UpdateFlow::handleProgress(qint64 received, qint64 total)
{
  if (m_silentDownload)
    Q_EMIT installProgress(received, total);
  if (!m_progress || total <= 0)
    return;
  // progress in KiB so the range fits in an int
  m_progress->setMaximum(static_cast<int>(total / 1024));
  m_progress->setValue(static_cast<int>(qMin(received, total) / 1024));
}

void UpdateFlow::handleUnpacking()
{
  if (m_silentDownload)
    Q_EMIT installProgress(0, 0);
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

  const auto result = startInstall(updatePath, m_updater->workDir(), m_version);
  if (!result.started()) {
    stopInstall(QStringLiteral("install did not start"));
    showInstallError(result.error, updatePath, result.canReveal);
    return;
  }

  m_downloaded = true;
  if (Settings::value(Settings::Gui::UpdateBlockedVersion).toString() == m_version)
    Settings::setValue(Settings::Gui::UpdateBlockedVersion, QVariant());

  // let the signal handlers return before the window closes
  QTimer::singleShot(0, this, &UpdateFlow::restartRequested);
}

void UpdateFlow::handleDownloadFailed(const QString &message)
{
  closeProgress();
  const auto silent = m_silentDownload;
  stopInstall(message);
  if (m_cancelled) {
    m_cancelled = false;
    return;
  }
  if (silent && !m_interactive) {
    qWarning("automatic update download failed: %s", qPrintable(message));
    return;
  }
  QMessageBox::warning(dialogParent(), tr("Update"), tr("The update could not be downloaded.\n\n%1").arg(message));
}

void UpdateFlow::stopInstall(const QString &reason)
{
  qInfo("update not installed: %s", qPrintable(reason));
  const auto wasSilent = m_silentDownload;
  m_installing = false;
  m_silentDownload = false;
  if (wasSilent)
    Q_EMIT installStopped();
  // the manual path stays available
  if (!m_version.isEmpty())
    Q_EMIT updateFound(m_version);
}

void UpdateFlow::showInstallError(InstallError error, const QString &updatePath, bool canReveal)
{
  QString text;
  switch (error) {
    using enum InstallError;
  case NotInBundle:
    text = tr("%1 does not run from an app bundle, so it cannot replace itself.").arg(kAppName);
    break;
  case NoWriteAccess:
    text = tr("You do not have permission to replace %1 in its folder.").arg(kAppName);
    break;
  case SignatureMismatch:
    text = tr("The downloaded update is not signed like the installed app. It was deleted and not installed.");
    break;
  case VersionMismatch:
    text = tr("The downloaded app is not the expected newer version. It was deleted and not installed.");
    break;
  case HelperMissing:
  case StartFailed:
    text = tr("The installer could not be started.");
    break;
  case Unsupported:
    text = tr("Automatic install is not supported on this system.");
    break;
  case None:
    return;
  }

  QString details;
  if (canReveal) {
    revealInFileManager(updatePath);
    details = tr("The update is shown in %1, you can install it by hand.").arg(kFileManager);
  }
  details += (details.isEmpty() ? QString() : QStringLiteral("\n\n")) +
             tr("Details are in %1.").arg(QDir::toNativeSeparators(update_install::updaterLogPath()));
  nonModalMessage(dialogParent(), tr("Update"), tr("The update was not installed. %1").arg(text), details)
      ->show();
}

void UpdateFlow::readInstallResult()
{
  QFile file(update_install::installResultPath());
  if (!file.exists())
    return;
  const auto content = file.open(QIODevice::ReadOnly) ? file.read(4096) : QByteArray();
  file.close();
  file.remove();

  const auto result = update_install::parseInstallResult(content);
  if (!result)
    return;
  qWarning("the last update was not installed: %s", qPrintable(result->status));

  const auto appExists = !result->app.isEmpty() && QFileInfo(result->app).isDir();
  if (appExists)
    revealInFileManager(result->app);
  const auto revealText =
      appExists ? tr("The downloaded version is shown in %1, you can also install it by hand.").arg(kFileManager)
                : QString();

  if (result->status != update_install::kResultPermissionDenied) {
    nonModalMessage(
        dialogParent(), tr("Update"), tr("The update could not be installed."),
        revealText + (revealText.isEmpty() ? QString() : QStringLiteral("\n\n")) +
            tr("Details are in %1.").arg(QDir::toNativeSeparators(update_install::updaterLogPath()))
    )
        ->show();
    return;
  }

  // do not try this version again on its own; the status bar still offers it
  if (appExists) {
    const QSettings info(result->app + QStringLiteral("/Contents/Info.plist"), QSettings::NativeFormat);
    if (const auto version = info.value(QStringLiteral("CFBundleVersion")).toString(); !version.isEmpty())
      Settings::setValue(Settings::Gui::UpdateBlockedVersion, version);
  }

  auto message = nonModalMessage(
      dialogParent(), tr("Update"), tr("macOS did not allow %1 to replace itself.").arg(kAppName),
      tr("To allow automatic updates, open System Settings > Privacy & Security > App Management and turn on %1. "
         "Then click \"Update available\" in the status bar.")
              .arg(kAppName) +
          (revealText.isEmpty() ? QString() : QStringLiteral("\n\n") + revealText)
  );
  const auto openSettings = message->addButton(tr("Open System Settings"), QMessageBox::ActionRole);
  connect(openSettings, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(kAppManagementUrl)); });
  message->show();
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

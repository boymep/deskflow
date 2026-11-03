/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateInstaller.h"

#include "UpdateUtils.h"
#include "common/UpdateInstall.h"
#include "common/VersionInfo.h"

#if defined(Q_OS_MACOS)
#include "MacCodeSignature.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace deskflow::gui::updates {

using update_install::appendUpdaterLog;

namespace {

void logStep(const QString &message)
{
  qInfo("update: %s", qPrintable(message));
  appendUpdaterLog(QStringLiteral("gui: ") + message);
}

/// Removes a download that failed the checks, so nothing untrusted stays around.
void discardWorkDir(const QString &workDir)
{
  if (workDir.isEmpty() || !QFileInfo(workDir).fileName().startsWith(update_install::kWorkDirPrefix))
    return;
  QDir(workDir).removeRecursively();
  logStep(QStringLiteral("removed %1").arg(workDir));
}

#if defined(Q_OS_MACOS)
InstallStart startMacInstall(const QString &updatePath, const QString &workDir, const QString &releaseVersion)
{
  const auto newApp = QDir::cleanPath(QFileInfo(updatePath).absoluteFilePath());
  const auto target = bundlePathFromAppDir(QCoreApplication::applicationDirPath());
  logStep(QStringLiteral("installing %1 to %2").arg(newApp, target));
  if (target.isEmpty()) {
    logStep(QStringLiteral("not installed: the app does not run from a .app bundle"));
    return {InstallError::NotInBundle, true};
  }

  const QFileInfo targetInfo(target);
  const QFileInfo parentInfo(targetInfo.absolutePath());
  // moving a folder to another parent also needs write access to the folder itself
  if (!parentInfo.isWritable() || !targetInfo.isWritable()) {
    logStep(QStringLiteral("not installed: no write access to %1").arg(target));
    return {InstallError::NoWriteAccess, true};
  }

  const auto check = checkBundleSignature(newApp);
  if (!check.valid) {
    logStep(QStringLiteral("not installed: %1").arg(check.error));
    discardWorkDir(workDir);
    return {InstallError::SignatureMismatch, false};
  }
  logStep(QStringLiteral("the signature is valid, bundle version %1").arg(check.version));

  if (!isAcceptableUpdateVersion(check.version, releaseVersion, QString::fromUtf8(kVersion))) {
    logStep(QStringLiteral("not installed: bundle version %1, release %2, running %3")
                .arg(check.version, releaseVersion, QString::fromUtf8(kVersion)));
    discardWorkDir(workDir);
    return {InstallError::VersionMismatch, false};
  }

  const auto helper = newApp + QStringLiteral("/Contents/MacOS/deskflow-core");
  const QFileInfo helperInfo(helper);
  if (!helperInfo.isFile() || !helperInfo.isExecutable() || helperInfo.isSymLink()) {
    logStep(QStringLiteral("not installed: the update has no install helper"));
    return {InstallError::HelperMissing, true};
  }

  const auto arguments =
      update_install::helperArguments({newApp, target, QCoreApplication::applicationPid()});
  if (!QProcess::startDetached(helper, arguments)) {
    logStep(QStringLiteral("not installed: could not start the install helper"));
    return {InstallError::StartFailed, true};
  }

  logStep(QStringLiteral("the install helper is running, quitting"));
  return {};
}
#endif

#if defined(Q_OS_WIN)
InstallStart startWindowsInstall(const QString &msiPath)
{
  const auto systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
  const auto msiexec = QDir::toNativeSeparators(QDir(systemRoot).filePath(QStringLiteral("System32/msiexec.exe")));
  QDir().mkpath(update_install::updaterLogDir());
  const auto msiLog =
      QDir::toNativeSeparators(QDir(update_install::updaterLogDir()).filePath(QStringLiteral("msiexec.log")));
  const auto msi = QDir::toNativeSeparators(QFileInfo(msiPath).absoluteFilePath());

  logStep(QStringLiteral("installing %1 with %2, log: %3").arg(msi, msiexec, msiLog));
  // an argument list, no shell; msiexec asks for elevation itself
  if (!QProcess::startDetached(msiexec, msiexecArguments(msi, msiLog))) {
    logStep(QStringLiteral("not installed: could not start %1").arg(msiexec));
    return {InstallError::StartFailed, true};
  }

  logStep(QStringLiteral("msiexec is running, quitting"));
  return {};
}
#endif

} // namespace

InstallStart startInstall(const QString &updatePath, const QString &workDir, const QString &releaseVersion)
{
#if defined(Q_OS_MACOS)
  return startMacInstall(updatePath, workDir, releaseVersion);
#elif defined(Q_OS_WIN)
  Q_UNUSED(workDir)
  Q_UNUSED(releaseVersion)
  return startWindowsInstall(updatePath);
#else
  Q_UNUSED(updatePath)
  Q_UNUSED(workDir)
  Q_UNUSED(releaseVersion)
  logStep(QStringLiteral("not installed: automatic install is not supported on this platform"));
  return {InstallError::Unsupported, true};
#endif
}

void revealInFileManager(const QString &path)
{
#if defined(Q_OS_MACOS)
  QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#elif defined(Q_OS_WIN)
  // absolute path from SystemRoot and an argument list, no shell
  const auto systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
  const auto explorer = QDir::toNativeSeparators(QDir(systemRoot).filePath(QStringLiteral("explorer.exe")));
  const auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  QProcess::startDetached(explorer, {QStringLiteral("/select,") + native});
#else
  Q_UNUSED(path)
#endif
}

void removeStaleWorkDirs()
{
  const QDir temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  const auto limit = QDateTime::currentDateTime().addDays(-1);
  const auto entries = temp.entryInfoList(
      {update_install::kWorkDirPrefix + QLatin1Char('*')}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks
  );
  for (const auto &entry : entries) {
    if (entry.lastModified() < limit)
      discardWorkDir(entry.absoluteFilePath());
  }
}

} // namespace deskflow::gui::updates

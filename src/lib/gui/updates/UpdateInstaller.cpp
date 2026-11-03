/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateInstaller.h"

#include "UpdateUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace deskflow::gui::updates {

namespace {

#if defined(Q_OS_MACOS)
constexpr int kCodesignTimeoutMs = 2 * 60 * 1000;
const auto kCodesign = QStringLiteral("/usr/bin/codesign");

/// Runs a tool with an argument list (no shell) and returns its merged output, or nothing on failure.
std::optional<QString> run(const QString &program, const QStringList &arguments, int *exitCode = nullptr)
{
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(program, arguments);
  if (!process.waitForFinished(kCodesignTimeoutMs) || process.exitStatus() != QProcess::NormalExit) {
    process.kill();
    process.waitForFinished();
    return std::nullopt;
  }
  if (exitCode)
    *exitCode = process.exitCode();
  return QString::fromUtf8(process.readAll());
}

/// The new app must be validly signed; if the running app has a real certificate, by the same identity.
bool verifySignature(const QString &runningApp, const QString &newApp)
{
  int exitCode = -1;
  const auto info = run(kCodesign, {QStringLiteral("-dv"), runningApp}, &exitCode);
  if (!info || exitCode != 0) {
    qWarning("update not installed: could not read the signature of the running app");
    return false;
  }

  QStringList arguments = {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict")};
  if (isAdhocSignature(*info)) {
    qInfo("the running app is signed ad-hoc, checking only that the update is validly signed");
  } else {
    const auto requirementOutput = run(kCodesign, {QStringLiteral("-d"), QStringLiteral("-r-"), runningApp}, &exitCode);
    const auto requirement = requirementOutput && exitCode == 0 ? designatedRequirement(*requirementOutput) : QString();
    if (requirement.isEmpty()) {
      qWarning("update not installed: could not read the designated requirement of the running app");
      return false;
    }
    qInfo("checking the update against the requirement: %s", qPrintable(requirement));
    // "=" marks the argument as requirement text instead of a file name
    arguments << QStringLiteral("-R") << QLatin1Char('=') + requirement;
  }
  arguments << newApp;

  const auto output = run(kCodesign, arguments, &exitCode);
  if (!output || exitCode != 0) {
    qWarning(
        "update not installed: the signature check failed, exit code: %d, output: %s", exitCode,
        output ? qPrintable(output->trimmed()) : "none"
    );
    return false;
  }
  return true;
}

bool startMacInstall(const QString &newApp, const QString &workDir)
{
  const auto target = bundlePathFromAppDir(QCoreApplication::applicationDirPath());
  if (target.isEmpty()) {
    qWarning(
        "update not installed: the app does not run from a .app bundle: %s",
        qPrintable(QCoreApplication::applicationDirPath())
    );
    return false;
  }

  const QFileInfo targetInfo(target);
  const QFileInfo parentInfo(targetInfo.absolutePath());
  // moving a folder to another parent also needs write access to the folder itself
  if (!parentInfo.isWritable() || !targetInfo.isWritable()) {
    qWarning("update not installed: no write access to %s", qPrintable(target));
    return false;
  }

  if (!verifySignature(target, newApp))
    return false;

  const auto cleanWorkDir = QDir::cleanPath(workDir);
  const auto scriptPath = QDir(cleanWorkDir).filePath(QStringLiteral("install.sh"));
  QFile script(scriptPath);
  if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate) || script.write(macInstallScript().toUtf8()) < 0) {
    qWarning("update not installed: could not write %s", qPrintable(scriptPath));
    return false;
  }
  script.close();

  const auto backup = cleanWorkDir + QStringLiteral("-backup");
  const auto log = cleanWorkDir + QStringLiteral(".log");
  const QStringList arguments = {
      scriptPath, QString::number(QCoreApplication::applicationPid()), newApp, target, cleanWorkDir, backup, log
  };
  if (!QProcess::startDetached(QStringLiteral("/bin/sh"), arguments)) {
    qWarning("update not installed: could not start the install helper");
    return false;
  }

  qInfo("installing the update to %s after quitting, log: %s", qPrintable(target), qPrintable(log));
  return true;
}
#endif

#if defined(Q_OS_WIN)
bool startWindowsInstall(const QString &msiPath, const QString &workDir)
{
  const auto systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
  const auto powerShell = QDir::toNativeSeparators(
      QDir(systemRoot).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"))
  );
  const auto exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  const auto script = windowsInstallScript(
      QDir::toNativeSeparators(QFileInfo(msiPath).absoluteFilePath()), exePath, QDir::toNativeSeparators(workDir),
      QCoreApplication::applicationPid()
  );

  // the script goes base64 encoded, so no part of it needs command line quoting
  const QStringList arguments = {
      QStringLiteral("-NoProfile"),
      QStringLiteral("-ExecutionPolicy"),
      QStringLiteral("Bypass"),
      QStringLiteral("-WindowStyle"),
      QStringLiteral("Hidden"),
      QStringLiteral("-EncodedCommand"),
      QString::fromLatin1(powerShellEncodedCommand(script))
  };
  if (!QProcess::startDetached(powerShell, arguments)) {
    qWarning("update not installed: could not start %s", qPrintable(powerShell));
    return false;
  }

  qInfo("installing %s after quitting", qPrintable(msiPath));
  return true;
}
#endif

} // namespace

bool startInstall(const QString &updatePath, const QString &workDir)
{
#if defined(Q_OS_MACOS)
  return startMacInstall(updatePath, workDir);
#elif defined(Q_OS_WIN)
  return startWindowsInstall(updatePath, workDir);
#else
  Q_UNUSED(updatePath)
  Q_UNUSED(workDir)
  qWarning("update not installed: automatic install is not supported on this platform");
  return false;
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

} // namespace deskflow::gui::updates

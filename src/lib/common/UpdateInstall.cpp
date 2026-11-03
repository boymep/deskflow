/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateInstall.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace deskflow::update_install {

namespace {

const auto kAppSuffix = QStringLiteral(".app");

bool isValidBundlePath(const QString &path)
{
  const QFileInfo info(path);
  return info.isAbsolute() && QDir::cleanPath(path) == path && path.endsWith(kAppSuffix) &&
         info.fileName().size() > kAppSuffix.size();
}

QString errorText(int error)
{
  return QString::fromLocal8Bit(std::strerror(error));
}

} // namespace

bool isHelperInvocation(const QStringList &arguments)
{
  return arguments.size() >= 2 && arguments.at(1) == kInstallUpdateOption;
}

std::optional<HelperArgs> parseHelperArgs(const QStringList &arguments, QString *error)
{
  const auto fail = [error](const QString &reason) -> std::optional<HelperArgs> {
    if (error)
      *error = reason;
    return std::nullopt;
  };

  if (!isHelperInvocation(arguments))
    return fail(QStringLiteral("not an install helper call"));
  if (arguments.size() != 5)
    return fail(QStringLiteral("expected: %1 <new.app> <target.app> <gui-pid>").arg(kInstallUpdateOption));

  HelperArgs args;
  args.newApp = arguments.at(2);
  args.target = arguments.at(3);
  if (!isValidBundlePath(args.newApp))
    return fail(QStringLiteral("the new app must be an absolute clean path to a .app: %1").arg(args.newApp));
  if (!isValidBundlePath(args.target))
    return fail(QStringLiteral("the target must be an absolute clean path to a .app: %1").arg(args.target));
  if (isInsideBundle(args.newApp, args.target) || isInsideBundle(args.target, args.newApp))
    return fail(QStringLiteral("the new app and the target overlap"));

  bool ok = false;
  args.guiPid = arguments.at(4).toLongLong(&ok);
  if (!ok || args.guiPid <= 1 || arguments.at(4) != QString::number(args.guiPid))
    return fail(QStringLiteral("invalid pid: %1").arg(arguments.at(4)));
  return args;
}

QStringList helperArguments(const HelperArgs &args)
{
  return {kInstallUpdateOption, args.newApp, args.target, QString::number(args.guiPid)};
}

QString backupPath(const QString &target, const QDateTime &now)
{
  return target + QStringLiteral(".old-") + now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

QString workDirOf(const QString &newApp)
{
  const QFileInfo app(QDir::cleanPath(newApp));
  const QFileInfo unpacked(app.absolutePath());
  const QFileInfo work(unpacked.absolutePath());
  if (!app.isAbsolute() || unpacked.fileName() != kUnpackDirName || !work.fileName().startsWith(kWorkDirPrefix) ||
      work.fileName().size() <= kWorkDirPrefix.size())
    return {};
  return work.absoluteFilePath();
}

bool isInsideBundle(const QString &path, const QString &bundle)
{
  const auto cleanPath = QDir::cleanPath(path);
  const auto cleanBundle = QDir::cleanPath(bundle);
  return cleanPath == cleanBundle || cleanPath.startsWith(cleanBundle + QLatin1Char('/'));
}

bool isPermissionError(int error)
{
  return error == EPERM || error == EACCES;
}

QString swapResultName(SwapResult result)
{
  switch (result) {
    using enum SwapResult;
  case Installed:
    return QStringLiteral("installed");
  case PermissionDenied:
    return QStringLiteral("permission denied");
  case Failed:
    return QStringLiteral("failed");
  case RollbackFailed:
    return QStringLiteral("rollback failed");
  }
  return {};
}

SwapResult swapBundles(const QString &newApp, const QString &target, const QString &backup, const SwapOps &ops)
{
  if (!ops.exists(newApp)) {
    ops.log(QStringLiteral("the new app is missing: %1").arg(newApp));
    return SwapResult::Failed;
  }
  if (ops.exists(backup)) {
    ops.log(QStringLiteral("the backup path already exists: %1").arg(backup));
    return SwapResult::Failed;
  }

  if (const auto error = ops.rename(target, backup); error != 0) {
    ops.log(QStringLiteral("could not move the old app aside: %1").arg(errorText(error)));
    return isPermissionError(error) ? SwapResult::PermissionDenied : SwapResult::Failed;
  }
  ops.log(QStringLiteral("moved the old app to %1").arg(backup));

  bool installed = false;
  bool permission = false;
  const auto error = ops.rename(newApp, target);
  if (error == 0) {
    installed = true;
  } else if (error == EXDEV) {
    ops.log(QStringLiteral("the new app is on another volume, copying it"));
    if (ops.copy(newApp, target)) {
      installed = true;
      if (!ops.remove(newApp))
        ops.log(QStringLiteral("could not remove the downloaded copy: %1").arg(newApp));
    } else {
      ops.log(QStringLiteral("could not copy the new app into place"));
      if (ops.exists(target) && !ops.remove(target)) {
        ops.log(QStringLiteral("could not remove the partial copy, the old app stays at %1").arg(backup));
        return SwapResult::RollbackFailed;
      }
    }
  } else {
    permission = isPermissionError(error);
    ops.log(QStringLiteral("could not move the new app into place: %1").arg(errorText(error)));
  }

  if (installed) {
    ops.log(QStringLiteral("the new app is at %1").arg(target));
    if (!ops.remove(backup))
      ops.log(QStringLiteral("could not remove the old app at %1").arg(backup));
    return SwapResult::Installed;
  }

  if (const auto restoreError = ops.rename(backup, target); restoreError != 0) {
    ops.log(
        QStringLiteral("could not restore the old app: %1, it is at %2").arg(errorText(restoreError), backup)
    );
    return SwapResult::RollbackFailed;
  }
  ops.log(QStringLiteral("restored the old app"));
  return permission ? SwapResult::PermissionDenied : SwapResult::Failed;
}

SwapOps systemSwapOps(const std::function<void(const QString &)> &log)
{
  SwapOps ops;
  ops.log = log;
  ops.rename = [](const QString &from, const QString &to) {
    const auto source = QFile::encodeName(from);
    const auto destination = QFile::encodeName(to);
    return std::rename(source.constData(), destination.constData()) == 0 ? 0 : errno;
  };
  ops.copy = [log](const QString &from, const QString &to) {
#if defined(Q_OS_MACOS)
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("/usr/bin/ditto"), {from, to});
    if (!process.waitForFinished(kToolTimeoutMs)) {
      process.kill();
      process.waitForFinished();
      log(QStringLiteral("ditto did not finish in time"));
      return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      log(QStringLiteral("ditto failed: %1").arg(QString::fromUtf8(process.readAll()).trimmed()));
      return false;
    }
    return true;
#else
    Q_UNUSED(from)
    Q_UNUSED(to)
    log(QStringLiteral("copying a bundle is only supported on macOS"));
    return false;
#endif
  };
  ops.remove = [](const QString &path) {
    const QFileInfo info(path);
    if (info.isSymLink() || info.isFile())
      return QFile::remove(path);
    return QDir(path).removeRecursively();
  };
  ops.exists = [](const QString &path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
  };
  return ops;
}

QByteArray formatInstallResult(const InstallResult &result)
{
  return QStringLiteral("status=%1\napp=%2\n").arg(result.status, result.app).toUtf8();
}

std::optional<InstallResult> parseInstallResult(const QByteArray &content)
{
  InstallResult result;
  for (const auto &line : QString::fromUtf8(content).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    const auto separator = line.indexOf(QLatin1Char('='));
    if (separator <= 0)
      continue;
    const auto key = line.left(separator);
    const auto value = line.mid(separator + 1).trimmed();
    if (key == QStringLiteral("status"))
      result.status = value;
    else if (key == QStringLiteral("app"))
      result.app = value;
  }
  if (result.status != kResultPermissionDenied && result.status != kResultFailed)
    return std::nullopt;
  return result;
}

QString updaterLogDir()
{
#if defined(Q_OS_MACOS)
  return QDir::homePath() + QStringLiteral("/Library/Logs/Deskflow");
#else
  return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Deskflow/logs");
#endif
}

QString updaterLogPath()
{
  return updaterLogDir() + QStringLiteral("/updater.log");
}

QString installResultPath()
{
  return updaterLogDir() + QStringLiteral("/updater-result.txt");
}

void appendUpdaterLog(const QString &message)
{
  QDir().mkpath(updaterLogDir());
  QFile file(updaterLogPath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    return;
  const auto line = QStringLiteral("%1 [%2] %3\n")
                        .arg(
                            QDateTime::currentDateTime().toString(Qt::ISODate),
                            QString::number(QCoreApplication::applicationPid()), message
                        );
  file.write(line.toUtf8());
}

} // namespace deskflow::update_install

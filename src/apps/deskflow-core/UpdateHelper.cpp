/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateHelper.h"

#include "common/CoreRecovery.h"
#include "common/UpdateInstall.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QThread>

#if defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc_info.h>
#endif

#include <vector>

using namespace deskflow::update_install;

namespace {

constexpr int kExitBadCall = 2;

void log(const QString &message)
{
  appendUpdaterLog(QStringLiteral("helper: ") + message);
}

#if defined(Q_OS_MACOS)
constexpr int kExitInstalled = 0;
constexpr int kExitNotInstalled = 1;
constexpr int kExitTimeout = 3;

/// Pids of processes whose executable is inside @p bundle, without this process.
QList<qint64> bundleProcesses(const QString &bundle)
{
  QList<qint64> result;
  const auto count = proc_listallpids(nullptr, 0);
  if (count <= 0)
    return result;
  // the list can grow between the two calls
  std::vector<pid_t> pids(static_cast<size_t>(count) + 64);
  const auto filled = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
  const auto self = QCoreApplication::applicationPid();
  for (int i = 0; i < filled && i < static_cast<int>(pids.size()); ++i) {
    const auto pid = pids[static_cast<size_t>(i)];
    if (pid <= 0 || pid == self)
      continue;
    char path[PROC_PIDPATHINFO_MAXSIZE] = {};
    if (proc_pidpath(pid, path, sizeof(path)) <= 0)
      continue;
    if (isInsideBundle(QString::fromUtf8(path), bundle))
      result << pid;
  }
  return result;
}

void runTool(const QString &program, const QStringList &arguments)
{
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(program, arguments);
  if (!process.waitForFinished(kToolTimeoutMs)) {
    process.kill();
    process.waitForFinished();
    log(QStringLiteral("%1 did not finish in time").arg(program));
    return;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    log(QStringLiteral("%1 failed: %2").arg(program, QString::fromUtf8(process.readAll()).trimmed()));
}

void openApp(const QString &app)
{
  log(QStringLiteral("opening %1").arg(app));
  if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-n"), app}))
    log(QStringLiteral("could not open %1").arg(app));
}

void writeResult(const QString &status, const QString &newApp)
{
  QDir().mkpath(updaterLogDir());
  QSaveFile file(installResultPath());
  if (!file.open(QIODevice::WriteOnly) || file.write(formatInstallResult({status, newApp})) < 0 || !file.commit())
    log(QStringLiteral("could not write the result file"));
}

int install(const HelperArgs &args)
{
  const auto self = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
  const auto newApp = QFileInfo(args.newApp).canonicalFilePath();
  if (newApp.isEmpty() || !isInsideBundle(self, newApp)) {
    log(QStringLiteral("the helper must run from the new app, it runs from %1").arg(self));
    return kExitBadCall;
  }
  const QFileInfo targetInfo(args.target);
  if (!targetInfo.isDir() || targetInfo.isSymLink()) {
    log(QStringLiteral("the target is not an app folder: %1").arg(args.target));
    return kExitBadCall;
  }
  const auto target = targetInfo.canonicalFilePath();

  QElapsedTimer timer;
  timer.start();
  while (true) {
    const auto guiRunning = deskflow::core_recovery::isProcessAlive(args.guiPid);
    const auto running = bundleProcesses(target);
    if (!guiRunning && running.isEmpty())
      break;
    if (timer.elapsed() > kWaitTimeoutMs) {
      QStringList pids;
      for (const auto pid : running)
        pids << QString::number(pid);
      log(QStringLiteral("the app did not quit within %1 s (gui running: %2, old bundle processes: %3), "
                         "nothing was installed")
              .arg(QString::number(kWaitTimeoutMs / 1000), guiRunning ? QStringLiteral("yes") : QStringLiteral("no"),
                   pids.isEmpty() ? QStringLiteral("none") : pids.join(QLatin1Char(','))));
      writeResult(kResultFailed, args.newApp);
      if (!guiRunning)
        openApp(args.target);
      return kExitTimeout;
    }
    QThread::msleep(kWaitPollMs);
  }
  log(QStringLiteral("the app has quit after %1 ms").arg(timer.elapsed()));

  const auto backup = backupPath(args.target, QDateTime::currentDateTime());
  const auto result = swapBundles(args.newApp, args.target, backup, systemSwapOps(log));
  log(QStringLiteral("swap result: %1").arg(swapResultName(result)));

  switch (result) {
    using enum SwapResult;
  case Installed: {
    runTool(QStringLiteral("/usr/bin/xattr"), {QStringLiteral("-dr"), QStringLiteral("com.apple.quarantine"), args.target});
    QFile::remove(installResultPath());
    openApp(args.target);
    if (const auto workDir = workDirOf(args.newApp); !workDir.isEmpty()) {
      if (QDir(workDir).removeRecursively())
        log(QStringLiteral("removed %1").arg(workDir));
      else
        log(QStringLiteral("could not remove %1").arg(workDir));
    }
    log(QStringLiteral("installed"));
    return kExitInstalled;
  }
  case PermissionDenied:
    writeResult(kResultPermissionDenied, args.newApp);
    openApp(args.target);
    return kExitNotInstalled;
  case Failed:
    writeResult(kResultFailed, args.newApp);
    openApp(args.target);
    return kExitNotInstalled;
  case RollbackFailed:
    writeResult(kResultFailed, args.newApp);
    // the old app is still complete at the backup path
    openApp(QFileInfo::exists(args.target) ? args.target : backup);
    return kExitNotInstalled;
  }
  return kExitNotInstalled;
}
#endif

} // namespace

int runInstallUpdateHelper(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const auto arguments = QCoreApplication::arguments();
  log(QStringLiteral("started: %1").arg(arguments.mid(1).join(QLatin1Char(' '))));

  QString error;
  const auto args = parseHelperArgs(arguments, &error);
  if (!args) {
    log(QStringLiteral("bad call: %1").arg(error));
    return kExitBadCall;
  }

#if defined(Q_OS_MACOS)
  return install(*args);
#else
  log(QStringLiteral("installing an app bundle is only supported on macOS"));
  return kExitBadCall;
#endif
}

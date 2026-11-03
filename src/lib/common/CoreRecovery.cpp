/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/CoreRecovery.h"

#include <QProcess>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace deskflow::core_recovery {

DuplicateCoreAction afterContactFailed(int failedAttempts, bool orphansTerminated)
{
  if (failedAttempts < kContactAttempts) {
    return DuplicateCoreAction::RetryContact;
  }
  return orphansTerminated ? DuplicateCoreAction::ReportError : DuplicateCoreAction::TerminateOrphans;
}

QList<qint64> parsePidList(const QByteArray &output, qint64 excludePid)
{
  QList<qint64> pids;
  for (const auto &line : output.split('\n')) {
    bool ok = false;
    const auto pid = line.trimmed().toLongLong(&ok);
    if (ok && pid > 0 && pid != excludePid && !pids.contains(pid)) {
      pids.append(pid);
    }
  }
  return pids;
}

QList<qint64> findUserProcesses(const QString &name, qint64 excludePid)
{
#if defined(Q_OS_UNIX)
  // matches the process name only, so cores from any bundle path are found
  QProcess pgrep;
  pgrep.start(
      QStringLiteral("/usr/bin/pgrep"), {QStringLiteral("-U"), QString::number(getuid()), QStringLiteral("-x"), name}
  );
  if (!pgrep.waitForFinished(2000) || pgrep.exitStatus() != QProcess::NormalExit) {
    pgrep.kill();
    return {};
  }
  return parsePidList(pgrep.readAllStandardOutput(), excludePid);
#else
  Q_UNUSED(name)
  Q_UNUSED(excludePid)
  return {};
#endif
}

bool isProcessAlive(qint64 pid)
{
#if defined(Q_OS_UNIX)
  return pid > 0 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#else
  Q_UNUSED(pid)
  return false;
#endif
}

void terminateProcess(qint64 pid, bool force)
{
#if defined(Q_OS_UNIX)
  if (pid > 0) {
    kill(static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM);
  }
#else
  Q_UNUSED(pid)
  Q_UNUSED(force)
#endif
}

} // namespace deskflow::core_recovery

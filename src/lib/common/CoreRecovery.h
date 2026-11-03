/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

/**
 * @brief Recovery from a core that refuses to start because another one seems to run.
 *
 * deskflow-core exits with s_exitDuplicate while its single-instance lock is
 * held. The GUI then tries to reach the existing core over IPC a few times;
 * if it never answers, it terminates leftover cores of the current user and
 * starts a fresh one, and only reports an error if that fails too.
 */
namespace deskflow::core_recovery {

//! Attempts to reach the existing core over IPC before giving up on it.
inline constexpr int kContactAttempts = 3;
//! Delay between those attempts.
inline constexpr int kContactRetryDelayMs = 2000;
//! Upper bound for one attempt (the IPC client itself retries for about 5 s).
inline constexpr int kContactTimeoutMs = 8000;
//! Time a leftover core gets to exit after SIGTERM before SIGKILL.
inline constexpr int kTerminateTimeoutMs = 3000;
//! Poll interval while waiting for leftover cores to exit.
inline constexpr int kTerminatePollMs = 100;

enum class DuplicateCoreAction
{
  RetryContact,     //!< try to reach the existing core again
  TerminateOrphans, //!< stop leftover cores and start a fresh one
  ReportError       //!< recovery already failed, tell the user
};

/**
 * @brief What to do after an attempt to contact the existing core failed.
 * @param failedAttempts failed attempts so far, including this one (>= 1)
 * @param orphansTerminated leftover cores were already terminated in this recovery
 */
DuplicateCoreAction afterContactFailed(int failedAttempts, bool orphansTerminated);

//! Parses one pid per line (pgrep output), skipping invalid lines and \p excludePid.
QList<qint64> parsePidList(const QByteArray &output, qint64 excludePid);

//! Pids of processes called \p name that the current user owns, without \p excludePid. Empty where unsupported.
QList<qint64> findUserProcesses(const QString &name, qint64 excludePid);

//! True while \p pid exists.
bool isProcessAlive(qint64 pid);

//! Sends SIGTERM, or SIGKILL when \p force is set. No-op where unsupported.
void terminateProcess(qint64 pid, bool force);

} // namespace deskflow::core_recovery

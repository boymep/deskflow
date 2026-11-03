/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreRecoveryTests.h"

#include "common/CoreRecovery.h"

#include <QProcess>

using namespace deskflow::core_recovery;

void CoreRecoveryTests::retriesBeforeTerminating()
{
  QCOMPARE(kContactAttempts, 3);
  QCOMPARE(afterContactFailed(1, false), DuplicateCoreAction::RetryContact);
  QCOMPARE(afterContactFailed(2, false), DuplicateCoreAction::RetryContact);
  QCOMPARE(afterContactFailed(3, false), DuplicateCoreAction::TerminateOrphans);
}

void CoreRecoveryTests::terminatesOnceThenReports()
{
  // after leftover cores were replaced, a fresh core that is again refused gets
  // the same retries, then the user sees an error instead of another kill round
  QCOMPARE(afterContactFailed(1, true), DuplicateCoreAction::RetryContact);
  QCOMPARE(afterContactFailed(3, true), DuplicateCoreAction::ReportError);
  QCOMPARE(afterContactFailed(7, true), DuplicateCoreAction::ReportError);
}

void CoreRecoveryTests::parsesPidList()
{
  QCOMPARE(parsePidList("123\n456\n", 0), (QList<qint64>{123, 456}));
  QCOMPARE(parsePidList(" 123 \n\nabc\n-5\n0\n123\n789", 789), (QList<qint64>{123}));
  QVERIFY(parsePidList("", 0).isEmpty());
}

void CoreRecoveryTests::findsOwnProcessByName()
{
#if defined(Q_OS_UNIX)
  QProcess child;
  child.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
  QVERIFY(child.waitForStarted());
  const auto pid = child.processId();

  QVERIFY(findUserProcesses(QStringLiteral("sleep"), 0).contains(pid));
  QVERIFY(!findUserProcesses(QStringLiteral("sleep"), pid).contains(pid));
  QVERIFY(!findUserProcesses(QStringLiteral("sleep-not-a-process"), 0).contains(pid));

  child.kill();
  child.waitForFinished(3000);
#else
  QSKIP("process lookup is Unix only");
#endif
}

void CoreRecoveryTests::terminatesChildProcess()
{
#if defined(Q_OS_UNIX)
  QProcess child;
  child.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
  QVERIFY(child.waitForStarted());
  const auto pid = child.processId();
  QVERIFY(isProcessAlive(pid));

  terminateProcess(pid, false);
  QVERIFY(child.waitForFinished(3000));
  QCOMPARE(child.exitStatus(), QProcess::CrashExit);
  QVERIFY(!isProcessAlive(pid));
#else
  QSKIP("process termination is Unix only");
#endif
}

QTEST_GUILESS_MAIN(CoreRecoveryTests)

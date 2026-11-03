/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateInstallTests.h"

#include "common/UpdateInstall.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>

#include <cerrno>

using namespace deskflow::update_install;

namespace {

const auto kNew = QStringLiteral("/tmp/deskflow-update-abc123/unpacked/Deskflow.app");
const auto kTarget = QStringLiteral("/Applications/Deskflow.app");
const auto kBackup = QStringLiteral("/Applications/Deskflow.app.old-20261104-120000");

/// In-memory file system: a set of paths, renames move a path, errors are injected per source path.
struct FakeFs
{
  QSet<QString> paths;
  QMap<QString, int> renameErrors; // source path -> errno
  bool copyOk = true;
  bool copyLeavesPartial = true;
  bool removeOk = true;
  QStringList log;

  SwapOps ops()
  {
    SwapOps ops;
    ops.rename = [this](const QString &from, const QString &to) {
      if (renameErrors.contains(from))
        return renameErrors.value(from);
      if (!paths.contains(from))
        return ENOENT;
      paths.remove(from);
      paths.insert(to);
      return 0;
    };
    ops.copy = [this](const QString &, const QString &to) {
      if (copyOk || copyLeavesPartial)
        paths.insert(to);
      return copyOk;
    };
    ops.remove = [this](const QString &path) {
      if (removeOk)
        paths.remove(path);
      return removeOk;
    };
    ops.exists = [this](const QString &path) { return paths.contains(path); };
    ops.log = [this](const QString &message) { log << message; };
    return ops;
  }
};

} // namespace

void UpdateInstallTests::parsesHelperArgs()
{
  const QStringList arguments = {QStringLiteral("deskflow-core"), kInstallUpdateOption, kNew, kTarget,
                                 QStringLiteral("4242")};
  QVERIFY(isHelperInvocation(arguments));
  const auto args = parseHelperArgs(arguments);
  QVERIFY(args.has_value());
  QCOMPARE(args->newApp, kNew);
  QCOMPARE(args->target, kTarget);
  QCOMPARE(args->guiPid, 4242);
}

void UpdateInstallTests::rejectsBadHelperArgs()
{
  const auto parse = [](const QStringList &rest) {
    QString error;
    const auto args = parseHelperArgs(QStringList{QStringLiteral("deskflow-core"), kInstallUpdateOption} + rest, &error);
    return !args.has_value() && !error.isEmpty();
  };
  QVERIFY(!isHelperInvocation({QStringLiteral("deskflow-core"), QStringLiteral("--server")}));
  QVERIFY(!parseHelperArgs({QStringLiteral("deskflow-core")}).has_value());
  QVERIFY(parse({kNew, kTarget}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("1"), QStringLiteral("extra")}));
  QVERIFY(parse({QStringLiteral("relative/Deskflow.app"), kTarget, QStringLiteral("42")}));
  QVERIFY(parse({kNew, QStringLiteral("/Applications/Deskflow"), QStringLiteral("42")}));
  QVERIFY(parse({kNew, QStringLiteral("/Applications/../Applications/Deskflow.app"), QStringLiteral("42")}));
  QVERIFY(parse({kNew, QStringLiteral("/.app"), QStringLiteral("42")}));
  QVERIFY(parse({kNew, kNew, QStringLiteral("42")}));
  QVERIFY(parse({kTarget + QStringLiteral("/Contents/Deskflow.app"), kTarget, QStringLiteral("42")}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("0")}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("1")}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("-5")}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("42abc")}));
  QVERIFY(parse({kNew, kTarget, QStringLiteral("+42")}));
}

void UpdateInstallTests::roundTripsHelperArgs()
{
  const HelperArgs args{kNew, kTarget, 777};
  const auto parsed = parseHelperArgs(QStringList{QStringLiteral("deskflow-core")} + helperArguments(args));
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->newApp, args.newApp);
  QCOMPARE(parsed->target, args.target);
  QCOMPARE(parsed->guiPid, args.guiPid);
}

void UpdateInstallTests::findsWorkDir()
{
  QCOMPARE(workDirOf(kNew), QStringLiteral("/tmp/deskflow-update-abc123"));
  QCOMPARE(workDirOf(QStringLiteral("/tmp/other/unpacked/Deskflow.app")), QString());
  QCOMPARE(workDirOf(QStringLiteral("/tmp/deskflow-update-abc/Deskflow.app")), QString());
  QCOMPARE(workDirOf(QStringLiteral("/tmp/deskflow-update-/unpacked/Deskflow.app")), QString());
  QCOMPARE(workDirOf(QStringLiteral("Deskflow.app")), QString());
}

void UpdateInstallTests::makesBackupPath()
{
  const QDateTime now(QDate(2026, 11, 4), QTime(12, 0, 0));
  QCOMPARE(backupPath(kTarget, now), kBackup);
  QVERIFY(isInsideBundle(kTarget + QStringLiteral("/Contents/MacOS/Deskflow"), kTarget));
  QVERIFY(!isInsideBundle(kBackup, kTarget));
}

void UpdateInstallTests::swapInstalls()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::Installed);
  QCOMPARE(fs.paths, QSet<QString>{kTarget});
}

void UpdateInstallTests::swapCopiesAcrossVolumes()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  fs.renameErrors[kNew] = EXDEV;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::Installed);
  QCOMPARE(fs.paths, QSet<QString>{kTarget});
}

void UpdateInstallTests::swapRollsBackWhenNewAppCannotMove()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  fs.renameErrors[kNew] = EPERM;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::PermissionDenied);
  QCOMPARE(fs.paths, (QSet<QString>{kNew, kTarget}));

  fs.renameErrors[kNew] = EIO;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::Failed);
  QCOMPARE(fs.paths, (QSet<QString>{kNew, kTarget}));
}

void UpdateInstallTests::swapRollsBackFailedCopy()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  fs.renameErrors[kNew] = EXDEV;
  fs.copyOk = false;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::Failed);
  QCOMPARE(fs.paths, (QSet<QString>{kNew, kTarget}));
}

void UpdateInstallTests::swapReportsPermissionOnFirstMove()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  fs.renameErrors[kTarget] = EACCES;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::PermissionDenied);
  QCOMPARE(fs.paths, (QSet<QString>{kNew, kTarget}));
}

void UpdateInstallTests::swapReportsFailedRollback()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget};
  fs.renameErrors[kNew] = EIO;
  fs.renameErrors[kBackup] = EIO;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::RollbackFailed);

  FakeFs partial;
  partial.paths = {kNew, kTarget};
  partial.renameErrors[kNew] = EXDEV;
  partial.copyOk = false;
  partial.removeOk = false;
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, partial.ops()), SwapResult::RollbackFailed);
  QVERIFY(partial.paths.contains(kBackup));
}

void UpdateInstallTests::swapRefusesExistingBackup()
{
  FakeFs fs;
  fs.paths = {kNew, kTarget, kBackup};
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, fs.ops()), SwapResult::Failed);
  QCOMPARE(fs.paths, (QSet<QString>{kNew, kTarget, kBackup}));

  FakeFs missing;
  missing.paths = {kTarget};
  QCOMPARE(swapBundles(kNew, kTarget, kBackup, missing.ops()), SwapResult::Failed);
  QCOMPARE(missing.paths, QSet<QString>{kTarget});
}

void UpdateInstallTests::roundTripsResultFile()
{
  const InstallResult result{kResultPermissionDenied, kNew};
  const auto parsed = parseInstallResult(formatInstallResult(result));
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->status, kResultPermissionDenied);
  QCOMPARE(parsed->app, kNew);
  QVERIFY(!parseInstallResult(QByteArrayLiteral("status=installed\n")).has_value());
  QVERIFY(!parseInstallResult(QByteArray()).has_value());
}

void UpdateInstallTests::swapsRealFolders()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QDir root(dir.path());
  const auto newApp = root.filePath(QStringLiteral("new/Deskflow.app"));
  const auto target = root.filePath(QStringLiteral("Applications/Deskflow.app"));
  const auto backup = backupPath(target, QDateTime::currentDateTime());
  QVERIFY(QDir().mkpath(newApp + QStringLiteral("/Contents")));
  QVERIFY(QDir().mkpath(target + QStringLiteral("/Contents")));
  QFile marker(newApp + QStringLiteral("/Contents/new"));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.close();

  const auto ops = systemSwapOps([](const QString &) {});
  QCOMPARE(swapBundles(newApp, target, backup, ops), SwapResult::Installed);
  QVERIFY(QFile::exists(target + QStringLiteral("/Contents/new")));
  QVERIFY(!QFile::exists(newApp));
  QVERIFY(!QFile::exists(backup));
}

QTEST_GUILESS_MAIN(UpdateInstallTests)

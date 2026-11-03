/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

/**
 * @brief Shared parts of the macOS update install helper (`deskflow-core --install-update`) and the GUI
 * that starts it: argument format, the bundle swap with rollback, the result file and the updater log.
 */
namespace deskflow::update_install {

/// Hidden deskflow-core mode: `deskflow-core --install-update <new.app> <target.app> <gui-pid>`.
inline const auto kInstallUpdateOption = QStringLiteral("--install-update");
/// Prefix of the temporary folders the GUI downloads updates into.
inline const auto kWorkDirPrefix = QStringLiteral("deskflow-update-");
/// The folder inside the work dir that holds the unpacked Deskflow.app.
inline const auto kUnpackDirName = QStringLiteral("unpacked");
/// Upper bound for the GUI and the old core to exit before the helper gives up.
inline constexpr int kWaitTimeoutMs = 30 * 1000;
inline constexpr int kWaitPollMs = 200;
/// Upper bound for one external tool run by the helper (ditto, xattr).
inline constexpr int kToolTimeoutMs = 2 * 60 * 1000;

/// Status values written to the result file when the helper did not install the update.
inline const auto kResultPermissionDenied = QStringLiteral("permission-denied");
inline const auto kResultFailed = QStringLiteral("failed");

struct HelperArgs
{
  QString newApp;
  QString target;
  qint64 guiPid = 0;
};

/// True when the process was started in the install helper mode. @param arguments includes the program.
bool isHelperInvocation(const QStringList &arguments);

/**
 * @brief Parses `<program> --install-update <new.app> <target.app> <gui-pid>`.
 * Both paths must be absolute, clean, different, and end with ".app"; the pid must be a positive number.
 * @param error is set to a short reason on failure.
 */
std::optional<HelperArgs> parseHelperArgs(const QStringList &arguments, QString *error = nullptr);

/// The arguments for the helper, without the program.
QStringList helperArguments(const HelperArgs &args);

/// Where the old bundle is moved during the swap: next to it, so the rename stays on the same volume.
QString backupPath(const QString &target, const QDateTime &now);

/**
 * @brief The download work dir of an unpacked update: ".../deskflow-update-XXXXXX/unpacked/Deskflow.app"
 * gives ".../deskflow-update-XXXXXX".
 * @return an empty string if the path does not have this layout, so nothing else is ever removed.
 */
QString workDirOf(const QString &newApp);

/// True if @p path is @p bundle or a file inside it.
bool isInsideBundle(const QString &path, const QString &bundle);

/// True for errno values that mean the OS refused the change (on macOS: App Management).
bool isPermissionError(int error);

enum class SwapResult
{
  Installed,        ///< the new app is at the target, the old one is gone (or its backup is logged)
  PermissionDenied, ///< nothing changed, the OS refused to move the old or the new app
  Failed,           ///< nothing changed for another reason
  RollbackFailed    ///< the old app could not be restored, it is at the backup path
};

QString swapResultName(SwapResult result);

/// File operations of the swap, so the rollback logic can be tested without touching real bundles.
struct SwapOps
{
  /// Returns 0 or the errno value.
  std::function<int(const QString &from, const QString &to)> rename;
  /// Copies a bundle to another volume. The destination must not exist.
  std::function<bool(const QString &from, const QString &to)> copy;
  std::function<bool(const QString &path)> remove;
  std::function<bool(const QString &path)> exists;
  std::function<void(const QString &message)> log;
};

/**
 * @brief Replaces @p target with @p newApp:
 * target -> backup, new -> target (a copy if the rename crosses volumes), then the backup is removed.
 * Any failure after the first rename restores the backup.
 */
SwapResult swapBundles(const QString &newApp, const QString &target, const QString &backup, const SwapOps &ops);

/// The real file operations: rename(2), /usr/bin/ditto and recursive removal.
SwapOps systemSwapOps(const std::function<void(const QString &)> &log);

struct InstallResult
{
  QString status;
  QString app;
};

/// The result file is "key=value" lines: "status=..." and "app=<the downloaded new app>".
QByteArray formatInstallResult(const InstallResult &result);
std::optional<InstallResult> parseInstallResult(const QByteArray &content);

/// ~/Library/Logs/Deskflow on macOS, <local app data>/Deskflow/logs elsewhere.
QString updaterLogDir();
QString updaterLogPath();
/// The helper leaves this file when it could not install; the GUI reads and removes it on the next start.
QString installResultPath();

/// Appends a time stamped line to the updater log. Never pass secrets.
void appendUpdaterLog(const QString &message);

} // namespace deskflow::update_install

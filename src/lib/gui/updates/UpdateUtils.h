/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace deskflow::gui::updates {

// Releases of the personal fork are published on a private GitHub repository.
const auto kRepoOwner = QStringLiteral("boymep");
const auto kRepoName = QStringLiteral("deskflow");
const auto kUrlLatestRelease =
    QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(kRepoOwner, kRepoName);
const auto kMacAssetName = QStringLiteral("Deskflow-arm64.zip");
const auto kWindowsAssetName = QStringLiteral("Deskflow-x64.msi");
const auto kAppBundleName = QStringLiteral("Deskflow.app");

// Size limits for everything read from the network.
constexpr qint64 kMaxJsonSize = 1024 * 1024;
constexpr qint64 kMaxShaFileSize = 1024;
constexpr qint64 kMaxZipSize = 300LL * 1024 * 1024;
constexpr int kMaxRedirects = 5;

/**
 * @brief Name of the release asset for this platform: the zip on macOS, the MSI on Windows.
 * The checksum asset is the same name plus ".sha256".
 */
QString platformAssetName();
QString shaAssetName(const QString &assetName);

/**
 * @brief Parses a four part numeric version, e.g. "1.27.0.27" or "v1.27.0.27".
 * @return the four parts, or nothing if the string is not exactly four numbers.
 */
std::optional<QList<int>> parseVersion(const QString &version);

/**
 * @brief Compares two four part versions.
 * @return a negative value if left < right, 0 if equal, positive if left > right.
 * Unparsable versions compare lower than any valid version.
 */
int compareVersions(const QString &left, const QString &right);

/**
 * @brief Parses the content of a sha256 file in `shasum -a 256` format: "<hex>  <name>".
 * @return the lower case hex digest, or nothing if the format is wrong or the name differs.
 */
std::optional<QByteArray> parseSha256File(const QByteArray &content, const QString &expectedName);

struct ReleaseAsset
{
  QUrl url;
  qint64 size = 0;
};

struct ReleaseInfo
{
  QString tag;
  QString version;
  ReleaseAsset zip;
  ReleaseAsset sha;
};

/**
 * @brief Parses the JSON from the GitHub "latest release" API.
 * @param error is set to a short description when parsing fails.
 * @param assetName the asset to look for, plus its ".sha256" file. Defaults to the platform asset.
 */
std::optional<ReleaseInfo> parseRelease(
    const QByteArray &json, QString *error = nullptr, const QString &assetName = platformAssetName()
);

/**
 * @brief Checks that an asset API url points at an asset of our repository on api.github.com.
 */
bool isAllowedAssetUrl(const QUrl &url);

/**
 * @brief Checks that a download redirect stays on https and on a GitHub download host
 * (github.com or *.githubusercontent.com).
 */
bool isAllowedRedirectUrl(const QUrl &url);

/**
 * @brief The .app bundle of the running app, derived from QCoreApplication::applicationDirPath(),
 * which is ".../Deskflow.app/Contents/MacOS" on macOS.
 * @return the bundle path, or an empty string if the directory is not inside a bundle.
 */
QString bundlePathFromAppDir(const QString &appDir);

/**
 * @brief Checks the output of `codesign -dv` for an ad-hoc signature: "Signature=adhoc" or no
 * "Authority=" line, which means no real certificate signed the app.
 */
bool isAdhocSignature(const QString &codesignInfo);

/**
 * @brief Takes the designated requirement from the output of `codesign -d -r-`,
 * the text after "designated => ".
 * @return the requirement, or an empty string if there is none.
 */
QString designatedRequirement(const QString &codesignOutput);

/**
 * @brief The /bin/sh script that installs a macOS update after the app has quit.
 *
 * Arguments: parent pid, new app, target app, work dir, backup path, log file.
 * It waits for the pid to exit, moves the target to the backup, moves the new app into place
 * (restoring the backup on failure), removes the quarantine flag, opens the target and
 * removes the backup and the work dir.
 */
QString macInstallScript();

/**
 * @brief Quotes text as a single quoted PowerShell string: 'text', with ' doubled.
 */
QString powerShellQuote(const QString &text);

/**
 * @brief The PowerShell script that installs a Windows update after the app has quit.
 *
 * It waits for the pid to exit, runs msiexec elevated (UAC) in passive mode, starts the app again
 * and removes the work dir.
 */
QString windowsInstallScript(const QString &msiPath, const QString &exePath, const QString &workDir, qint64 pid);

/**
 * @brief Encodes a script for `powershell -EncodedCommand`: base64 of the UTF-16LE text.
 * This avoids any command line quoting of the script.
 */
QByteArray powerShellEncodedCommand(const QString &script);

} // namespace deskflow::gui::updates

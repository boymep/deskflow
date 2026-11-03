/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
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
 * @brief The version gate for a downloaded app: its signed bundle version must be the release version
 * and newer than the running version.
 */
bool isAcceptableUpdateVersion(const QString &bundleVersion, const QString &releaseVersion, const QString &currentVersion);

/**
 * @brief Arguments for msiexec.exe: install the msi with a progress bar and no questions, no reboot,
 * a verbose log, and start the app again when done (DESKFLOW_RELAUNCH, see deploy/windows/wix-patch.xml.in).
 */
QStringList msiexecArguments(const QString &msiPath, const QString &logPath);

} // namespace deskflow::gui::updates

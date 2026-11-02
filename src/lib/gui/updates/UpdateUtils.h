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
const auto kZipAssetName = QStringLiteral("Deskflow-arm64.zip");
const auto kShaAssetName = QStringLiteral("Deskflow-arm64.zip.sha256");
const auto kAppBundleName = QStringLiteral("Deskflow.app");

// Size limits for everything read from the network.
constexpr qint64 kMaxJsonSize = 1024 * 1024;
constexpr qint64 kMaxShaFileSize = 1024;
constexpr qint64 kMaxZipSize = 300LL * 1024 * 1024;
constexpr int kMaxTokenLength = 255;
constexpr int kMaxRedirects = 5;

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
 */
std::optional<ReleaseInfo> parseRelease(const QByteArray &json, QString *error = nullptr);

/**
 * @brief Checks that an asset API url points at an asset of our repository on api.github.com.
 */
bool isAllowedAssetUrl(const QUrl &url);

/**
 * @brief Checks that a download redirect stays on https and on a GitHub download host
 * (github.com or *.githubusercontent.com). The token is never sent to these hosts.
 */
bool isAllowedRedirectUrl(const QUrl &url);

/**
 * @brief Basic sanity check of a GitHub token before it is stored or sent: printable ASCII, no spaces.
 */
bool isPlausibleToken(const QString &token);

} // namespace deskflow::gui::updates

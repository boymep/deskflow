/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace deskflow::gui::updates {

namespace {

constexpr int kMaxAssets = 100;

void setError(QString *error, const QString &message)
{
  if (error)
    *error = message;
}

bool isHttpsWithoutExtras(const QUrl &url)
{
  return url.isValid() && url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty() &&
         (url.port() == -1 || url.port() == 443) && !url.host().isEmpty();
}

std::optional<ReleaseAsset> parseAsset(const QJsonObject &asset, qint64 maxSize, QString *error)
{
  const auto url = asset.value(QStringLiteral("url"));
  const auto size = asset.value(QStringLiteral("size"));
  const auto state = asset.value(QStringLiteral("state"));
  if (!url.isString() || !size.isDouble()) {
    setError(error, QStringLiteral("release asset has no valid url or size"));
    return std::nullopt;
  }
  if (!state.isUndefined() && state.toString() != QStringLiteral("uploaded")) {
    setError(error, QStringLiteral("release asset is not fully uploaded"));
    return std::nullopt;
  }

  ReleaseAsset result;
  result.url = QUrl(url.toString(), QUrl::StrictMode);
  if (!isAllowedAssetUrl(result.url)) {
    setError(error, QStringLiteral("release asset url is not on the expected host"));
    return std::nullopt;
  }

  const double sizeValue = size.toDouble();
  if (sizeValue <= 0 || sizeValue > static_cast<double>(maxSize)) {
    setError(error, QStringLiteral("release asset size is out of range"));
    return std::nullopt;
  }
  result.size = static_cast<qint64>(sizeValue);
  return result;
}

} // namespace

std::optional<QList<int>> parseVersion(const QString &version)
{
  static const QRegularExpression re(QStringLiteral("^v?(\\d{1,9})\\.(\\d{1,9})\\.(\\d{1,9})\\.(\\d{1,9})$"));
  const auto match = re.match(version);
  if (!match.hasMatch())
    return std::nullopt;

  QList<int> parts;
  for (int i = 1; i <= 4; ++i)
    parts.append(match.captured(i).toInt());
  return parts;
}

int compareVersions(const QString &left, const QString &right)
{
  const auto leftParts = parseVersion(left);
  const auto rightParts = parseVersion(right);
  if (!leftParts || !rightParts)
    return (leftParts ? 1 : 0) - (rightParts ? 1 : 0);

  for (int i = 0; i < 4; ++i) {
    if (leftParts->at(i) != rightParts->at(i))
      return leftParts->at(i) < rightParts->at(i) ? -1 : 1;
  }
  return 0;
}

std::optional<QByteArray> parseSha256File(const QByteArray &content, const QString &expectedName)
{
  if (content.size() > kMaxShaFileSize)
    return std::nullopt;

  // `shasum` marks binary mode with '*' before the name
  static const QRegularExpression re(QStringLiteral("^([0-9a-fA-F]{64}) [ *](\\S+)$"));
  const auto match = re.match(QString::fromLatin1(content.trimmed()));
  if (!match.hasMatch() || match.captured(2) != expectedName)
    return std::nullopt;

  return match.captured(1).toLower().toLatin1();
}

QString platformAssetName()
{
#if defined(Q_OS_WIN)
  return kWindowsAssetName;
#else
  return kMacAssetName;
#endif
}

QString shaAssetName(const QString &assetName)
{
  return assetName + QStringLiteral(".sha256");
}

std::optional<ReleaseInfo> parseRelease(const QByteArray &json, QString *error, const QString &assetName)
{
  if (json.size() > kMaxJsonSize) {
    setError(error, QStringLiteral("release data is too large"));
    return std::nullopt;
  }

  QJsonParseError parseError;
  const auto doc = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    setError(error, QStringLiteral("release data is not a JSON object"));
    return std::nullopt;
  }

  const auto root = doc.object();
  const auto tag = root.value(QStringLiteral("tag_name"));
  if (!tag.isString() || !parseVersion(tag.toString())) {
    setError(error, QStringLiteral("release tag is not a version like v1.2.3.4"));
    return std::nullopt;
  }
  if (root.value(QStringLiteral("draft")).toBool(false) || root.value(QStringLiteral("prerelease")).toBool(false)) {
    setError(error, QStringLiteral("release is a draft or pre-release"));
    return std::nullopt;
  }

  const auto assets = root.value(QStringLiteral("assets"));
  if (!assets.isArray() || assets.toArray().size() > kMaxAssets) {
    setError(error, QStringLiteral("release has no valid asset list"));
    return std::nullopt;
  }

  ReleaseInfo info;
  info.tag = tag.toString();
  info.version = info.tag.mid(info.tag.startsWith(QLatin1Char('v')) ? 1 : 0);

  const auto shaName = shaAssetName(assetName);
  bool hasZip = false;
  bool hasSha = false;
  for (const auto &value : assets.toArray()) {
    if (!value.isObject())
      continue;
    const auto asset = value.toObject();
    const auto name = asset.value(QStringLiteral("name")).toString();
    if (name == assetName && !hasZip) {
      const auto parsed = parseAsset(asset, kMaxZipSize, error);
      if (!parsed)
        return std::nullopt;
      info.zip = *parsed;
      hasZip = true;
    } else if (name == shaName && !hasSha) {
      const auto parsed = parseAsset(asset, kMaxShaFileSize, error);
      if (!parsed)
        return std::nullopt;
      info.sha = *parsed;
      hasSha = true;
    }
  }

  if (!hasZip || !hasSha) {
    setError(error, QStringLiteral("release is missing %1 or %2").arg(assetName, shaName));
    return std::nullopt;
  }
  return info;
}

bool isAllowedAssetUrl(const QUrl &url)
{
  if (!isHttpsWithoutExtras(url) || url.host() != QStringLiteral("api.github.com") || url.hasQuery() ||
      url.hasFragment())
    return false;

  static const QRegularExpression re(
      QStringLiteral("^/repos/%1/%2/releases/assets/\\d{1,20}$").arg(kRepoOwner, kRepoName)
  );
  return re.match(url.path(QUrl::FullyEncoded)).hasMatch();
}

bool isAllowedRedirectUrl(const QUrl &url)
{
  if (!isHttpsWithoutExtras(url))
    return false;

  const auto host = url.host();
  const auto suffix = QStringLiteral(".githubusercontent.com");
  return host == QStringLiteral("github.com") || (host.endsWith(suffix) && host.size() > suffix.size());
}

bool isPlausibleToken(const QString &token)
{
  if (token.isEmpty() || token.size() > kMaxTokenLength)
    return false;

  for (const auto c : token) {
    if (c.unicode() <= 0x20 || c.unicode() >= 0x7f)
      return false;
  }
  return true;
}

} // namespace deskflow::gui::updates

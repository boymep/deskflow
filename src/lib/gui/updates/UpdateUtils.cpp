/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateUtils.h"

#include <QDir>
#include <QFileInfo>
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


QString bundlePathFromAppDir(const QString &appDir)
{
  const QFileInfo macOs(QDir::cleanPath(appDir));
  const QFileInfo contents(macOs.absolutePath());
  const QFileInfo bundle(contents.absolutePath());
  if (appDir.isEmpty() || macOs.fileName() != QStringLiteral("MacOS") ||
      contents.fileName() != QStringLiteral("Contents") || !bundle.fileName().endsWith(QStringLiteral(".app")) ||
      bundle.fileName() == QStringLiteral(".app"))
    return {};
  return bundle.absoluteFilePath();
}

bool isAdhocSignature(const QString &codesignInfo)
{
  const auto lines = codesignInfo.split(QLatin1Char('\n'));
  bool hasAuthority = false;
  for (const auto &line : lines) {
    const auto trimmed = line.trimmed();
    if (trimmed == QStringLiteral("Signature=adhoc"))
      return true;
    if (trimmed.startsWith(QStringLiteral("Authority=")))
      hasAuthority = true;
  }
  return !hasAuthority;
}

QString designatedRequirement(const QString &codesignOutput)
{
  const auto prefix = QStringLiteral("designated => ");
  const auto lines = codesignOutput.split(QLatin1Char('\n'));
  for (const auto &line : lines) {
    // an ad-hoc signature only has an implicit requirement, printed as "# designated => cdhash ..."
    if (line.startsWith(prefix))
      return line.mid(prefix.size()).trimmed();
  }
  return {};
}

QString macInstallScript()
{
  return QStringLiteral(R"sh(#!/bin/sh
# Deskflow update helper: installs the new app after the old one has quit and opens it.
# Arguments: pid new_app target work_dir backup log
pid="$1"
new_app="$2"
target="$3"
work_dir="$4"
backup="$5"
log="$6"

exec >>"$log" 2>&1
echo "$(/bin/date): installing $new_app to $target"

waited=0
while kill -0 "$pid" 2>/dev/null; do
  if [ "$waited" -ge 60 ]; then
    echo "the app did not quit within 30 seconds, nothing was installed"
    exit 1
  fi
  waited=$((waited + 1))
  /bin/sleep 0.5
done

if [ -e "$backup" ]; then
  echo "the backup path already exists, nothing was installed: $backup"
  /usr/bin/open "$target"
  exit 1
fi

if ! /bin/mv "$target" "$backup"; then
  echo "could not move the old app aside, nothing was installed"
  /usr/bin/open "$target"
  exit 1
fi

if ! /bin/mv "$new_app" "$target"; then
  echo "could not move the new app into place, restoring the old app"
  /bin/rm -rf "$target"
  /bin/mv "$backup" "$target"
  /usr/bin/open "$target"
  exit 1
fi

/usr/bin/xattr -dr com.apple.quarantine "$target" 2>/dev/null
/usr/bin/open "$target"
/bin/rm -rf "$backup" "$work_dir"
echo "$(/bin/date): installed"
)sh");
}

QString powerShellQuote(const QString &text)
{
  auto escaped = text;
  escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
  return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString windowsInstallScript(const QString &msiPath, const QString &exePath, const QString &workDir, qint64 pid)
{
  // Start-Process joins ArgumentList without quoting, so the msi path gets its own double quotes.
  // A Windows path cannot contain a double quote.
  const auto msiArguments = QStringLiteral("/i \"%1\" /passive /norestart").arg(msiPath);
  return QStringLiteral("$ErrorActionPreference = 'SilentlyContinue'\n"
                        "Wait-Process -Id %1 -Timeout 30\n"
                        "try {\n"
                        "  Start-Process -FilePath 'msiexec.exe' -ArgumentList %2 -Verb RunAs -Wait -ErrorAction Stop\n"
                        "} catch {\n"
                        "}\n"
                        "Start-Process -FilePath %3\n"
                        "Remove-Item -LiteralPath %4 -Recurse -Force\n")
      .arg(QString::number(pid), powerShellQuote(msiArguments), powerShellQuote(exePath), powerShellQuote(workDir));
}

QByteArray powerShellEncodedCommand(const QString &script)
{
  QByteArray utf16;
  utf16.reserve(script.size() * 2);
  for (const auto c : script) {
    utf16.append(static_cast<char>(c.unicode() & 0xff));
    utf16.append(static_cast<char>(c.unicode() >> 8));
  }
  return utf16.toBase64();
}

} // namespace deskflow::gui::updates

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateUtilsTests.h"

#include "gui/updates/UpdateUtils.h"

using namespace deskflow::gui::updates;

namespace {

const auto kHash = QByteArrayLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
const auto kTag = QStringLiteral("v1.27.0.30");
const auto kAssetPath = QStringLiteral("/repos/boymep/deskflow/releases/assets/");
const auto kAssetBase = QStringLiteral("https://api.github.com") + kAssetPath;

QByteArray releaseJson(
    const QString &tag = QStringLiteral("v1.27.0.30"), const QString &zipUrl = kAssetBase + QStringLiteral("11"),
    qint64 zipSize = 50000000, const QString &extra = {}
)
{
  return QStringLiteral(R"({"tag_name":"%1","draft":false,"prerelease":false,%4"assets":[)"
                        R"({"name":"other.txt","url":"https://example.com/x","size":1},)"
                        R"({"name":"Deskflow-arm64.zip","url":"%2","size":%3,"state":"uploaded"},)"
                        R"({"name":"Deskflow-arm64.zip.sha256","url":"%5","size":85,"state":"uploaded"}]})")
      .arg(tag, zipUrl, QString::number(zipSize), extra, kAssetBase + QStringLiteral("12"))
      .toUtf8();
}

} // namespace

void UpdateUtilsTests::parseVersion()
{
  QCOMPARE(deskflow::gui::updates::parseVersion("1.27.0.27").value_or(QList<int>()), QList<int>({1, 27, 0, 27}));
  QCOMPARE(deskflow::gui::updates::parseVersion("v1.27.0.27").value_or(QList<int>()), QList<int>({1, 27, 0, 27}));
  QVERIFY(!deskflow::gui::updates::parseVersion("1.27.0"));
  QVERIFY(!deskflow::gui::updates::parseVersion("1.27.0.27-rc1"));
  QVERIFY(!deskflow::gui::updates::parseVersion("x1.27.0.27"));
  QVERIFY(!deskflow::gui::updates::parseVersion("1.27.0.1234567890"));
  QVERIFY(!deskflow::gui::updates::parseVersion(""));
}

void UpdateUtilsTests::compareVersions()
{
  using deskflow::gui::updates::compareVersions;
  QCOMPARE(compareVersions("1.27.0.27", "1.27.0.27"), 0);
  QVERIFY(compareVersions("1.27.0.28", "1.27.0.27") > 0);
  QVERIFY(compareVersions("1.27.0.9", "1.27.0.10") < 0); // numeric, not text
  QVERIFY(compareVersions("1.28.0.0", "1.27.0.99") > 0);
  QVERIFY(compareVersions("2.0.0.0", "1.99.99.99") > 0);
  QVERIFY(compareVersions("v1.27.0.28", "1.27.0.27") > 0);
  QVERIFY(compareVersions("garbage", "1.0.0.0") < 0);
  QVERIFY(compareVersions("1.0.0.0", "garbage") > 0);
}

void UpdateUtilsTests::parseSha256File()
{
  using deskflow::gui::updates::parseSha256File;
  const auto name = QStringLiteral("Deskflow-arm64.zip");
  QCOMPARE(parseSha256File(kHash + "  Deskflow-arm64.zip\n", name).value_or(QByteArray()), kHash);
  QCOMPARE(parseSha256File(kHash + " *Deskflow-arm64.zip", name).value_or(QByteArray()), kHash);
  QCOMPARE(parseSha256File(kHash.toUpper() + "  Deskflow-arm64.zip", name).value_or(QByteArray()), kHash);
  QVERIFY(!parseSha256File(kHash + "  dist/Deskflow-arm64.zip", name));
  QVERIFY(!parseSha256File(kHash + "  Other.zip", name));
  QVERIFY(!parseSha256File(kHash.left(63) + "  Deskflow-arm64.zip", name));
  QVERIFY(!parseSha256File(kHash + "  Deskflow-arm64.zip\n" + kHash + "  Deskflow-arm64.zip", name));
  QVERIFY(!parseSha256File(kHash, name));
  QVERIFY(!parseSha256File(QByteArray(2000, ' ') + kHash + "  Deskflow-arm64.zip", name));
}

void UpdateUtilsTests::parseRelease_valid()
{
  QString error;
  const auto release = parseRelease(releaseJson(), &error);
  QVERIFY2(release, qPrintable(error));
  QCOMPARE(release->tag, QStringLiteral("v1.27.0.30"));
  QCOMPARE(release->version, QStringLiteral("1.27.0.30"));
  QCOMPARE(release->zip.url, QUrl(kAssetBase + QStringLiteral("11")));
  QCOMPARE(release->zip.size, 50000000);
  QCOMPARE(release->sha.url, QUrl(kAssetBase + QStringLiteral("12")));
  QCOMPARE(release->sha.size, 85);
}

void UpdateUtilsTests::parseRelease_invalid()
{
  QVERIFY(!parseRelease("not json"));
  QVERIFY(!parseRelease("[]"));
  QVERIFY(!parseRelease(R"({"tag_name":"v1.27.0.30"})"));
  QVERIFY(!parseRelease(R"({"tag_name":5,"assets":[]})"));
  QVERIFY(!parseRelease(releaseJson(QStringLiteral("v1.27.0"))));
  QVERIFY(!parseRelease(releaseJson(QStringLiteral("latest"))));
  QVERIFY(!parseRelease(releaseJson(kTag, QStringLiteral("https://evil.example.com") + kAssetPath + "11")));
  QVERIFY(!parseRelease(releaseJson(kTag, QStringLiteral("http://api.github.com") + kAssetPath + "11")));
  QVERIFY(!parseRelease(releaseJson(kTag, kAssetBase + QStringLiteral("11"), 301LL * 1024 * 1024)));
  QVERIFY(!parseRelease(releaseJson(kTag, kAssetBase + QStringLiteral("11"), 0)));
  const auto draft = QStringLiteral(R"("draft":true,)");
  QVERIFY(!parseRelease(releaseJson(kTag, kAssetBase + QStringLiteral("11"), 50000000, draft)));
  QVERIFY(!parseRelease(QByteArray(kMaxJsonSize + 1, ' ')));

  // missing sha asset
  const auto noSha = QByteArrayLiteral(
      R"({"tag_name":"v1.27.0.30","assets":[{"name":"Deskflow-arm64.zip",)"
      R"("url":"https://api.github.com/repos/boymep/deskflow/releases/assets/11","size":10}]})"
  );
  QString error;
  QVERIFY(!parseRelease(noSha, &error));
  QVERIFY(!error.isEmpty());
}

void UpdateUtilsTests::assetUrl()
{
  QVERIFY(isAllowedAssetUrl(QUrl(kAssetBase + QStringLiteral("123"))));
  QVERIFY(!isAllowedAssetUrl(QUrl(kAssetBase + QStringLiteral("123?x=1"))));
  QVERIFY(!isAllowedAssetUrl(QUrl(QStringLiteral("https://api.github.com/repos/other/deskflow/releases/assets/1"))));
  QVERIFY(!isAllowedAssetUrl(QUrl(kAssetBase + QStringLiteral("1/../2"))));
  QVERIFY(!isAllowedAssetUrl(QUrl(QStringLiteral("https://user@api.github.com") + kAssetPath + "1")));
  QVERIFY(!isAllowedAssetUrl(QUrl(QStringLiteral("https://api.github.com:8443") + kAssetPath + "1")));
  QVERIFY(!isAllowedAssetUrl(QUrl(QStringLiteral("https://api.github.com.evil.com") + kAssetPath + "1")));
}

void UpdateUtilsTests::redirectUrl()
{
  QVERIFY(isAllowedRedirectUrl(QUrl(QStringLiteral("https://objects.githubusercontent.com/a?sig=1"))));
  QVERIFY(isAllowedRedirectUrl(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/a"))));
  QVERIFY(isAllowedRedirectUrl(QUrl(QStringLiteral("https://github.com/boymep/deskflow/releases/download/x"))));
  QVERIFY(isAllowedRedirectUrl(QUrl(QStringLiteral("https://OBJECTS.GitHubUserContent.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("http://objects.githubusercontent.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://githubusercontent.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://evilgithubusercontent.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://objects.githubusercontent.com.evil.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://api.github.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://gist.github.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://user:pw@github.com/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("https://github.com:444/a"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl(QStringLiteral("file:///etc/passwd"))));
  QVERIFY(!isAllowedRedirectUrl(QUrl()));
}

void UpdateUtilsTests::token()
{
  QVERIFY(isPlausibleToken(QStringLiteral("github_pat_11ABCDEFG0123456789_abcdefghijklmnopqrstuvwxyz")));
  QVERIFY(!isPlausibleToken(QString()));
  QVERIFY(!isPlausibleToken(QStringLiteral("abc def")));
  QVERIFY(!isPlausibleToken(QStringLiteral("abc\r\nX-Evil: 1")));
  QVERIFY(!isPlausibleToken(QStringLiteral("töken")));
  QVERIFY(!isPlausibleToken(QString(kMaxTokenLength + 1, QLatin1Char('a'))));
}

QTEST_MAIN(UpdateUtilsTests)

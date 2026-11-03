/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class UpdateUtilsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void parseVersion();
  void compareVersions();
  void parseSha256File();
  void parseRelease_valid();
  void parseRelease_platformAsset();
  void parseRelease_invalid();
  void assetUrl();
  void redirectUrl();
  void bundlePath();
  void adhocSignature();
  void designatedRequirement();
  void macInstallScript();
  void powerShellQuote();
  void windowsInstallScript();
  void powerShellEncodedCommand();
};

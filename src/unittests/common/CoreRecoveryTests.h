/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class CoreRecoveryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void retriesBeforeTerminating();
  void terminatesOnceThenReports();
  void parsesPidList();
  void findsOwnProcessByName();
  void terminatesChildProcess();
};

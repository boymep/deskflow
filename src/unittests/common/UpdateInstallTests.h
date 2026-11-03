/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class UpdateInstallTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void parsesHelperArgs();
  void rejectsBadHelperArgs();
  void roundTripsHelperArgs();
  void findsWorkDir();
  void makesBackupPath();
  void swapInstalls();
  void swapCopiesAcrossVolumes();
  void swapRollsBackWhenNewAppCannotMove();
  void swapRollsBackFailedCopy();
  void swapReportsPermissionOnFirstMove();
  void swapReportsFailedRollback();
  void swapRefusesExistingBackup();
  void roundTripsResultFile();
  void swapsRealFolders();
};

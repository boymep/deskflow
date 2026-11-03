/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QStatusBar>

#include "common/Enums.h"

class QPushButton;
class QLabel;
class QProgressBar;

using ProcessState = deskflow::core::ProcessState;
using ConnectionState = deskflow::core::ConnectionState;

class StatusBar : public QStatusBar
{
  Q_OBJECT
public:
  explicit StatusBar(QWidget *parent = nullptr);
  void setStatus(ConnectionState connectionState, ProcessState processState, bool isServer);
  void setServerClients(const QStringList &clients);
  void setSecurityIconVisible(bool visible);
  void setConnectionInterval(int newInterval);
  bool securityIconVisible() const;
  void updateSecurityInfo(bool encrypted);
  void setSecurityIcon(bool encrypted);
  void setSecurityLevel(const QString &securityLevel);
  void setBtnFingerprintVisible(bool visible);
  /// Offers an update that is not installed automatically: the "Update available" button.
  void updateFound(const QString &version);
  /// Shows "Installing update" with a small progress bar while an update installs in the background.
  void updateInstalling(const QString &version);
  /// Download progress of the installing update; total <= 0 shows a busy bar.
  void updateInstallProgress(qint64 received, qint64 total);
  void updateInstallStopped();

Q_SIGNALS:
  void requestShowMyFingerprints();
  void requestUpdateVersion();

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  void updateTimerLabel();
  QPushButton *m_btnFingerprint = nullptr;
  QLabel *m_lblSecurityIcon = nullptr;
  QLabel *m_lblStatus = nullptr;
  QPushButton *m_btnUpdate = nullptr;
  QLabel *m_lblInstalling = nullptr;
  QProgressBar *m_progressInstall = nullptr;
  bool m_encrypted = false;
  QString m_securityLevel;
  int m_connectionInterval = -1;
  QTimer *m_retryTimer = nullptr;
};

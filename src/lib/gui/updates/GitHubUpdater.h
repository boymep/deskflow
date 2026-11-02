/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "UpdateUtils.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace deskflow::gui::updates {

/**
 * @brief Checks the private GitHub repository for a newer release, downloads the app zip
 * into the Downloads folder, verifies its SHA-256 and unpacks it (macOS).
 *
 * Only outgoing HTTPS requests are made. The token goes to api.github.com only: redirects
 * are followed by hand and never carry the Authorization header.
 */
class GitHubUpdater : public QObject
{
  Q_OBJECT
public:
  explicit GitHubUpdater(QObject *parent = nullptr);
  ~GitHubUpdater() override;

  /// Starts a check. Emits noToken, checkFailed, upToDate or updateAvailable.
  void checkLatest();
  /// Downloads the release found by the last successful check.
  void downloadLatest();
  void cancel();
  bool isBusy() const;
  QString latestVersion() const;

Q_SIGNALS:
  void noToken();
  void checkFailed(const QString &message);
  void upToDate(const QString &version);
  void updateAvailable(const QString &version);
  void downloadProgress(qint64 received, qint64 total);
  void unpacking();
  void downloadFinished(const QString &appPath);
  void downloadFailed(const QString &message);

private:
  enum class Phase
  {
    Idle,
    Checking,
    FetchingSha,
    FetchingZip,
    Unpacking
  };

  void fetch(const QUrl &url, bool withToken, const QByteArray &accept, qint64 maxSize);
  void handleMetaData();
  void handleReadyRead();
  void handleFinished();
  void handleRedirect();
  void handleBody();
  void startZipDownload();
  void verifyZip();
  void unpack(const QString &zipPath);
  void finishUnpack(int exitCode);
  void fail(const QString &message);
  QString httpErrorMessage(int status) const;
  QString networkErrorMessage() const;
  void resetTransfer();

  QNetworkAccessManager *m_network = nullptr;
  QPointer<QNetworkReply> m_reply;
  QPointer<QProcess> m_process;
  Phase m_phase = Phase::Idle;
  QString m_token;
  QByteArray m_accept;
  qint64 m_maxSize = 0;
  qint64 m_received = 0;
  int m_redirects = 0;
  QString m_abortReason;
  QByteArray m_buffer;
  QFile m_file;
  QCryptographicHash m_hash{QCryptographicHash::Sha256};
  std::optional<ReleaseInfo> m_release;
  QByteArray m_expectedSha;
  QString m_zipPath;
  QString m_unpackDir;
};

} // namespace deskflow::gui::updates

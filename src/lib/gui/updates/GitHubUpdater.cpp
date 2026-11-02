/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "GitHubUpdater.h"

#include "UpdateTokenStore.h"
#include "common/Constants.h"
#include "common/VersionInfo.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace deskflow::gui::updates {

namespace {

constexpr int kTransferTimeoutMs = 30 * 1000;
constexpr int kUnpackTimeoutMs = 5 * 60 * 1000;
constexpr int kMaxUniqueNames = 100;
const auto kAcceptJson = QByteArrayLiteral("application/vnd.github+json");
const auto kAcceptBinary = QByteArrayLiteral("application/octet-stream");

bool isRedirectStatus(int status)
{
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/// Returns "<base><suffix>" or "<base>-N<suffix>" for the first name that does not exist yet.
QString uniquePath(const QString &base, const QString &suffix)
{
  for (int i = 1; i <= kMaxUniqueNames; ++i) {
    const auto path = i == 1 ? base + suffix : QStringLiteral("%1-%2%3").arg(base, QString::number(i), suffix);
    if (!QFileInfo::exists(path))
      return path;
  }
  return {};
}

} // namespace

GitHubUpdater::GitHubUpdater(QObject *parent) : QObject(parent), m_network{new QNetworkAccessManager(this)}
{
  // no caching or cookies are used, see fetch()
}

GitHubUpdater::~GitHubUpdater()
{
  // stop quietly, nobody is listening any more
  if (m_reply) {
    m_reply->disconnect(this);
    m_reply->abort();
  }
  if (m_process) {
    m_process->disconnect(this);
    m_process->kill();
  }
  if (m_file.isOpen()) {
    m_file.close();
    m_file.remove();
  }
}

bool GitHubUpdater::isBusy() const
{
  return m_phase != Phase::Idle;
}

QString GitHubUpdater::latestVersion() const
{
  return m_release ? m_release->version : QString();
}

void GitHubUpdater::checkLatest()
{
  if (isBusy())
    return;

  m_token = UpdateTokenStore::readToken();
  if (!isPlausibleToken(m_token)) {
    m_token.clear();
    qDebug("no update token in the keychain, skipping the update check");
    Q_EMIT noToken();
    return;
  }

  qDebug("checking for updates at: %s", qPrintable(kUrlLatestRelease));
  m_release.reset();
  m_redirects = 0;
  m_phase = Phase::Checking;
  fetch(QUrl(kUrlLatestRelease), true, kAcceptJson, kMaxJsonSize);
}

void GitHubUpdater::downloadLatest()
{
  if (isBusy() || !m_release)
    return;

  m_token = UpdateTokenStore::readToken();
  if (!isPlausibleToken(m_token)) {
    m_token.clear();
    Q_EMIT noToken();
    return;
  }

  m_redirects = 0;
  m_phase = Phase::FetchingSha;
  fetch(m_release->sha.url, true, kAcceptBinary, kMaxShaFileSize);
}

void GitHubUpdater::cancel()
{
  if (m_reply) {
    m_abortReason = tr("The download was cancelled.");
    m_reply->abort();
  }
  if (m_process) {
    m_process->kill();
  }
}

void GitHubUpdater::fetch(const QUrl &url, bool withToken, const QByteArray &accept, qint64 maxSize)
{
  resetTransfer();
  m_accept = accept;
  m_maxSize = maxSize;

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setTransferTimeout(kTransferTimeoutMs);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("%1/%2").arg(kAppName, kVersion));
  request.setRawHeader("Accept", accept);
  if (withToken) {
    // only ever used for the hard coded api.github.com URLs
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toLatin1());
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
  }

  m_reply = m_network->get(request);
  connect(m_reply, &QNetworkReply::metaDataChanged, this, &GitHubUpdater::handleMetaData);
  connect(m_reply, &QNetworkReply::readyRead, this, &GitHubUpdater::handleReadyRead);
  connect(m_reply, &QNetworkReply::finished, this, &GitHubUpdater::handleFinished);
  if (m_phase == Phase::FetchingZip) {
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
      Q_EMIT downloadProgress(received, m_release ? m_release->zip.size : 0);
    });
  }
}

void GitHubUpdater::resetTransfer()
{
  m_received = 0;
  m_buffer.clear();
  m_abortReason.clear();
  m_hash.reset();
}

void GitHubUpdater::handleMetaData()
{
  if (!m_reply)
    return;

  const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto length = m_reply->header(QNetworkRequest::ContentLengthHeader);
  if (status == 200 && length.isValid() && length.toLongLong() > m_maxSize) {
    m_abortReason = tr("The server sent more data than expected.");
    m_reply->abort();
  }
}

void GitHubUpdater::handleReadyRead()
{
  if (!m_reply)
    return;

  const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto data = m_reply->readAll();
  if (status != 200)
    return; // error and redirect bodies are not used

  m_received += data.size();
  if (m_received > m_maxSize) {
    m_abortReason = tr("The server sent more data than expected.");
    m_reply->abort();
    return;
  }

  if (m_phase == Phase::FetchingZip) {
    m_hash.addData(data);
    if (m_file.write(data) != data.size()) {
      m_abortReason = tr("Could not write the download to %1.").arg(m_file.fileName());
      m_reply->abort();
    }
  } else {
    m_buffer.append(data);
  }
}

void GitHubUpdater::handleFinished()
{
  if (!m_reply)
    return;

  // drain anything that arrived together with the finished signal
  if (m_abortReason.isEmpty() && m_reply->bytesAvailable() > 0)
    handleReadyRead();

  const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto error = m_reply->error();
  m_reply->deleteLater();

  if (!m_abortReason.isEmpty()) {
    const auto reason = m_abortReason;
    m_reply = nullptr;
    fail(reason);
    return;
  }

  if (isRedirectStatus(status)) {
    handleRedirect();
    return;
  }

  if (status != 200) {
    const auto message = status > 0 ? httpErrorMessage(status) : networkErrorMessage();
    qWarning("update request failed, http status: %d, network error: %d", status, static_cast<int>(error));
    m_reply = nullptr;
    fail(message);
    return;
  }

  if (error != QNetworkReply::NoError) {
    const auto message = networkErrorMessage();
    qWarning("update request failed, network error: %d", static_cast<int>(error));
    m_reply = nullptr;
    fail(message);
    return;
  }

  m_reply = nullptr;
  handleBody();
}

void GitHubUpdater::handleRedirect()
{
  const auto location = m_reply->header(QNetworkRequest::LocationHeader).toUrl();
  const auto target = m_reply->url().resolved(location);
  m_reply = nullptr;

  // only asset downloads are expected to redirect, the API call itself must not
  if (m_phase != Phase::FetchingSha && m_phase != Phase::FetchingZip) {
    fail(tr("GitHub answered with an unexpected redirect."));
    return;
  }
  if (++m_redirects > kMaxRedirects) {
    fail(tr("Too many redirects while downloading the update."));
    return;
  }
  if (!isAllowedRedirectUrl(target)) {
    qWarning("update download redirected to a host that is not allowed: %s", qPrintable(target.host()));
    fail(tr("The download was redirected to an unexpected address."));
    return;
  }

  // the signed URL carries its own authorization, the token must not leave api.github.com
  qDebug("following update download redirect to host: %s", qPrintable(target.host()));
  fetch(target, false, m_accept, m_maxSize);
}

void GitHubUpdater::handleBody()
{
  m_redirects = 0;

  if (m_phase == Phase::Checking) {
    QString error;
    m_release = parseRelease(m_buffer, &error);
    m_buffer.clear();
    m_token.clear();
    m_phase = Phase::Idle;
    if (!m_release) {
      qWarning("invalid release data from github: %s", qPrintable(error));
      Q_EMIT checkFailed(tr("The release information from GitHub is not valid: %1").arg(error));
      return;
    }

    if (compareVersions(m_release->version, kVersion) > 0) {
      qInfo("update available: %s, current version: %s", qPrintable(m_release->version), kVersion);
      Q_EMIT updateAvailable(m_release->version);
    } else {
      qDebug("current version %s is up to date, latest release: %s", kVersion, qPrintable(m_release->version));
      Q_EMIT upToDate(m_release->version);
    }
    return;
  }

  if (m_phase == Phase::FetchingSha) {
    const auto sha = parseSha256File(m_buffer, kZipAssetName);
    m_buffer.clear();
    if (!sha) {
      fail(tr("The checksum file of the release is not valid."));
      return;
    }
    m_expectedSha = *sha;
    startZipDownload();
    return;
  }

  if (m_phase == Phase::FetchingZip)
    verifyZip();
}

void GitHubUpdater::startZipDownload()
{
  const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (downloads.isEmpty() || !QDir().mkpath(downloads)) {
    fail(tr("Could not find the Downloads folder."));
    return;
  }

  const auto base = QDir(downloads).filePath(QStringLiteral("Deskflow-%1").arg(m_release->version));
  m_zipPath = uniquePath(base, QStringLiteral(".zip"));
  const auto partPath = uniquePath(base, QStringLiteral(".zip.download"));
  if (m_zipPath.isEmpty() || partPath.isEmpty()) {
    fail(tr("Could not choose a file name in the Downloads folder."));
    return;
  }

  m_file.setFileName(partPath);
  if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    fail(tr("Could not create %1.").arg(partPath));
    return;
  }

  qInfo("downloading update %s to: %s", qPrintable(m_release->version), qPrintable(partPath));
  m_phase = Phase::FetchingZip;
  fetch(m_release->zip.url, true, kAcceptBinary, kMaxZipSize);
}

void GitHubUpdater::verifyZip()
{
  m_token.clear();
  m_file.close();

  const auto actual = m_hash.result().toHex();
  if (m_received != m_release->zip.size || actual != m_expectedSha) {
    qWarning(
        "update checksum mismatch, expected: %s, actual: %s, size: %lld", m_expectedSha.constData(),
        actual.constData(), m_received
    );
    m_file.remove();
    fail(tr("The downloaded file does not match its checksum and was deleted."));
    return;
  }

  if (!m_file.rename(m_zipPath)) {
    m_file.remove();
    fail(tr("Could not save %1.").arg(m_zipPath));
    return;
  }

  qInfo("update download verified, sha256: %s", m_expectedSha.constData());
  unpack(m_zipPath);
}

void GitHubUpdater::unpack(const QString &zipPath)
{
#if defined(Q_OS_MACOS)
  const auto downloads = QFileInfo(zipPath).absolutePath();
  m_unpackDir = uniquePath(QDir(downloads).filePath(QStringLiteral("Deskflow-%1").arg(m_release->version)), {});
  if (m_unpackDir.isEmpty() || !QDir().mkdir(m_unpackDir)) {
    fail(tr("Could not create a folder for the update in %1.").arg(downloads));
    return;
  }

  m_phase = Phase::Unpacking;
  Q_EMIT unpacking();

  m_process = new QProcess(this);
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
    finishUnpack(exitStatus == QProcess::NormalExit ? exitCode : -1);
  });
  connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      finishUnpack(-1);
  });
  QTimer::singleShot(kUnpackTimeoutMs, m_process, [this] {
    if (m_process)
      m_process->kill();
  });

  // absolute path and argument list, no shell
  const QStringList arguments = {QStringLiteral("-x"), QStringLiteral("-k"), zipPath, m_unpackDir};
  m_process->start(QStringLiteral("/usr/bin/ditto"), arguments);
#else
  m_phase = Phase::Idle;
  Q_EMIT downloadFinished(zipPath);
#endif
}

void GitHubUpdater::finishUnpack(int exitCode)
{
  if (!m_process || m_phase != Phase::Unpacking)
    return;

  m_process->deleteLater();
  m_process = nullptr;

  if (exitCode != 0) {
    qWarning("unpacking the update failed, exit code: %d", exitCode);
    fail(tr("Could not unpack the update. The verified zip is at %1.").arg(m_zipPath));
    return;
  }

  const QDir dir(m_unpackDir);
  const auto entries = dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  const QFileInfo app(dir.filePath(kAppBundleName));
  if (entries != QStringList{kAppBundleName} || !app.isDir() || app.isSymLink()) {
    fail(tr("The update does not contain %1 as expected. Nothing was installed.").arg(kAppBundleName));
    return;
  }

  const auto appPath = app.absoluteFilePath();
  qInfo("update unpacked to: %s", qPrintable(appPath));
#if defined(Q_OS_MACOS)
  QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), appPath});
#endif
  m_phase = Phase::Idle;
  Q_EMIT downloadFinished(appPath);
}

void GitHubUpdater::fail(const QString &message)
{
  const auto phase = m_phase;
  m_phase = Phase::Idle;
  m_token.clear();
  m_buffer.clear();
  m_redirects = 0;
  if (m_file.isOpen()) {
    m_file.close();
    m_file.remove();
  }

  if (phase == Phase::Checking)
    Q_EMIT checkFailed(message);
  else
    Q_EMIT downloadFailed(message);
}

QString GitHubUpdater::httpErrorMessage(int status) const
{
  if (status == 401 || status == 403)
    return tr("GitHub rejected the token (HTTP %1). Check that it is valid and can read %2/%3.")
        .arg(QString::number(status), kRepoOwner, kRepoName);
  if (status == 404)
    return tr("No release was found (HTTP 404). Either nothing is published yet or the token cannot see %1/%2.")
        .arg(kRepoOwner, kRepoName);
  return tr("GitHub answered with HTTP %1.").arg(status);
}

QString GitHubUpdater::networkErrorMessage() const
{
  // errorString() can contain the signed download URL, so it is not shown or logged
  switch (m_reply ? m_reply->error() : QNetworkReply::UnknownNetworkError) {
  case QNetworkReply::TimeoutError:
  case QNetworkReply::OperationCanceledError:
    return tr("The connection timed out.");
  case QNetworkReply::HostNotFoundError:
    return tr("Could not reach GitHub. Check the Internet connection.");
  case QNetworkReply::SslHandshakeFailedError:
    return tr("A secure connection to GitHub could not be established.");
  default:
    return tr("A network error occurred while talking to GitHub.");
  }
}

} // namespace deskflow::gui::updates

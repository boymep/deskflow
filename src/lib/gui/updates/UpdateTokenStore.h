/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui::updates {

/**
 * @brief Keeps the GitHub token for update checks in the OS credential store. The token is never written
 * to settings files or logs.
 * macOS: Keychain generic password, service "deskflow-updates", account "github-token".
 * Windows: Credential Manager generic credential, target "deskflow-updates/github-token".
 * On other platforms there is no storage and all calls fail.
 */
class UpdateTokenStore
{
public:
  inline static const auto kService = QStringLiteral("deskflow-updates");
  inline static const auto kAccount = QStringLiteral("github-token");

  /// Windows Credential Manager target name.
  inline static const auto kTarget = QStringLiteral("deskflow-updates/github-token");

  static bool isSupported();
  /// Human readable name of the store for messages: "macOS Keychain" or "Windows Credential Manager".
  static QString storeName();
  static bool hasToken();
  /// @return the token, or an empty string if there is none.
  static QString readToken();
  static bool saveToken(const QString &token);
  static bool removeToken();
};

} // namespace deskflow::gui::updates

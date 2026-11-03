/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::gui::updates {

struct BundleSignatureCheck
{
  bool valid = false;
  /// Why the check failed, for the log.
  QString error;
  /// CFBundleVersion from the signed Info.plist of the checked bundle.
  QString version;
};

/**
 * @brief Checks that the bundle at @p bundlePath has a valid signature (all architectures, strict, nested
 * code included) that satisfies the designated requirement of the running app.
 *
 * So only an app signed with the same identity and with the same bundle id is accepted. If the running
 * app is signed ad-hoc, its requirement is its own code hash and no other bundle passes.
 */
BundleSignatureCheck checkBundleSignature(const QString &bundlePath);

} // namespace deskflow::gui::updates

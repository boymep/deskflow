/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdateTokenStore.h"

#include <QDebug>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace deskflow::gui::updates {

#if defined(Q_OS_MACOS)

namespace {

/// Owns a CoreFoundation reference.
template <typename T> class CfRef
{
public:
  explicit CfRef(T ref = nullptr) : m_ref(ref)
  {
  }
  ~CfRef()
  {
    if (m_ref)
      CFRelease(m_ref);
  }
  CfRef(const CfRef &) = delete;
  CfRef &operator=(const CfRef &) = delete;
  T get() const
  {
    return m_ref;
  }

private:
  T m_ref;
};

CFStringRef toCfString(const QString &string)
{
  return string.toCFString();
}

/// Base query for our Keychain item. The caller owns the returned dictionary.
CFMutableDictionaryRef baseQuery()
{
  auto query = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
  );
  CfRef<CFStringRef> service(toCfString(UpdateTokenStore::kService));
  CfRef<CFStringRef> account(toCfString(UpdateTokenStore::kAccount));
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query, kSecAttrService, service.get());
  CFDictionarySetValue(query, kSecAttrAccount, account.get());
  return query;
}

} // namespace

bool UpdateTokenStore::isSupported()
{
  return true;
}

bool UpdateTokenStore::hasToken()
{
  CfRef<CFMutableDictionaryRef> query(baseQuery());
  CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
  CFDictionarySetValue(query.get(), kSecReturnAttributes, kCFBooleanTrue);
  CFTypeRef result = nullptr;
  const auto status = SecItemCopyMatching(query.get(), &result);
  CfRef<CFTypeRef> owned(result);
  return status == errSecSuccess;
}

QString UpdateTokenStore::readToken()
{
  CfRef<CFMutableDictionaryRef> query(baseQuery());
  CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
  CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
  CFTypeRef result = nullptr;
  const auto status = SecItemCopyMatching(query.get(), &result);
  CfRef<CFTypeRef> owned(result);
  if (status != errSecSuccess || !result || CFGetTypeID(result) != CFDataGetTypeID()) {
    if (status != errSecItemNotFound)
      qWarning("could not read the update token from the keychain, status: %d", static_cast<int>(status));
    return {};
  }

  const auto data = static_cast<CFDataRef>(result);
  const auto length = CFDataGetLength(data);
  if (length <= 0 || length > 4096)
    return {};
  return QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), static_cast<qsizetype>(length));
}

bool UpdateTokenStore::saveToken(const QString &token)
{
  const auto bytes = token.toUtf8();
  CfRef<CFDataRef> data(
      CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size())
  );

  CfRef<CFMutableDictionaryRef> query(baseQuery());
  CfRef<CFMutableDictionaryRef> update(CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks
  ));
  CFDictionarySetValue(update.get(), kSecValueData, data.get());

  auto status = SecItemUpdate(query.get(), update.get());
  if (status == errSecItemNotFound) {
    CFDictionarySetValue(query.get(), kSecValueData, data.get());
    CFDictionarySetValue(query.get(), kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
    CfRef<CFStringRef> label(toCfString(QStringLiteral("Deskflow update token")));
    CFDictionarySetValue(query.get(), kSecAttrLabel, label.get());
    status = SecItemAdd(query.get(), nullptr);
  }

  if (status != errSecSuccess) {
    qWarning("could not save the update token to the keychain, status: %d", static_cast<int>(status));
    return false;
  }
  return true;
}

bool UpdateTokenStore::removeToken()
{
  CfRef<CFMutableDictionaryRef> query(baseQuery());
  const auto status = SecItemDelete(query.get());
  if (status != errSecSuccess && status != errSecItemNotFound) {
    qWarning("could not remove the update token from the keychain, status: %d", static_cast<int>(status));
    return false;
  }
  return true;
}

#else

bool UpdateTokenStore::isSupported()
{
  return false;
}

bool UpdateTokenStore::hasToken()
{
  return false;
}

QString UpdateTokenStore::readToken()
{
  return {};
}

bool UpdateTokenStore::saveToken(const QString &)
{
  return false;
}

bool UpdateTokenStore::removeToken()
{
  return false;
}

#endif

} // namespace deskflow::gui::updates

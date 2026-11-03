/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacCodeSignature.h"

#include <QFile>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace deskflow::gui::updates {

namespace {

/// Releases a Core Foundation object when it goes out of scope.
template <typename T> class CFHolder
{
public:
  CFHolder() = default;
  explicit CFHolder(T ref) : m_ref{ref}
  {
  }
  ~CFHolder()
  {
    if (m_ref)
      CFRelease(m_ref);
  }
  CFHolder(const CFHolder &) = delete;
  CFHolder &operator=(const CFHolder &) = delete;
  T get() const
  {
    return m_ref;
  }
  T *out()
  {
    return &m_ref;
  }

private:
  T m_ref = nullptr;
};

QString fromCFString(CFStringRef text)
{
  if (!text)
    return {};
  const auto length = CFStringGetLength(text);
  QString result(length, Qt::Uninitialized);
  CFStringGetCharacters(text, CFRangeMake(0, length), reinterpret_cast<UniChar *>(result.data()));
  return result;
}

QString statusText(OSStatus status)
{
  CFHolder<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));
  return QStringLiteral("%1 (%2)").arg(fromCFString(message.get()), QString::number(status));
}

} // namespace

BundleSignatureCheck checkBundleSignature(const QString &bundlePath)
{
  BundleSignatureCheck check;

  CFHolder<SecCodeRef> self;
  if (const auto status = SecCodeCopySelf(kSecCSDefaultFlags, self.out()); status != errSecSuccess) {
    check.error = QStringLiteral("could not get the code of the running app: %1").arg(statusText(status));
    return check;
  }

  CFHolder<SecRequirementRef> requirement;
  if (const auto status = SecCodeCopyDesignatedRequirement(self.get(), kSecCSDefaultFlags, requirement.out());
      status != errSecSuccess) {
    check.error = QStringLiteral("the running app has no designated requirement: %1").arg(statusText(status));
    return check;
  }

  CFHolder<CFStringRef> requirementText;
  SecRequirementCopyString(requirement.get(), kSecCSDefaultFlags, requirementText.out());
  qInfo("checking the update against the requirement: %s", qPrintable(fromCFString(requirementText.get())));

  const auto path = QFile::encodeName(bundlePath);
  CFHolder<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(path.constData()), path.size(), true
  ));
  if (!url.get()) {
    check.error = QStringLiteral("invalid bundle path");
    return check;
  }

  CFHolder<SecStaticCodeRef> code;
  if (const auto status = SecStaticCodeCreateWithPath(url.get(), kSecCSDefaultFlags, code.out());
      status != errSecSuccess) {
    check.error = QStringLiteral("could not read the signature of the update: %1").arg(statusText(status));
    return check;
  }

  constexpr SecCSFlags kFlags = kSecCSCheckAllArchitectures | kSecCSStrictValidate | kSecCSCheckNestedCode;
  if (const auto status = SecStaticCodeCheckValidity(code.get(), kFlags, requirement.get());
      status != errSecSuccess) {
    check.error = QStringLiteral("the update is not signed like the running app: %1").arg(statusText(status));
    return check;
  }

  // the Info.plist is sealed by the signature that was just checked
  CFHolder<CFDictionaryRef> info;
  if (const auto status = SecCodeCopySigningInformation(code.get(), kSecCSRequirementInformation, info.out());
      status != errSecSuccess) {
    check.error = QStringLiteral("could not read the signing information: %1").arg(statusText(status));
    return check;
  }
  const auto plist = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info.get(), kSecCodeInfoPList));
  const auto version =
      plist && CFGetTypeID(plist) == CFDictionaryGetTypeID()
          ? static_cast<CFStringRef>(CFDictionaryGetValue(plist, CFSTR("CFBundleVersion")))
          : nullptr;
  if (!version || CFGetTypeID(version) != CFStringGetTypeID()) {
    check.error = QStringLiteral("the update has no bundle version");
    return check;
  }

  check.version = fromCFString(version);
  check.valid = true;
  return check;
}

} // namespace deskflow::gui::updates

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXInputSourceCache.h"

#include "base/Log.h"
#include "platform/OSXAutoTypes.h"
#include "platform/OSXMainThread.h"
#include "platform/OSXWindowsKeyboard.h"

#include <Carbon/Carbon.h>

#include <algorithm>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// Every function in this file that touches TIS/TSM/LMGet* runs on the main
// thread: the public *OnMain entry points check requireMainThread(), the rest
// are only called from them. Writers are therefore serialized by the main
// thread; m_mutex only protects the snapshot pointer for readers.

namespace deskflow::osx {

namespace {

std::string toStdString(CFStringRef string)
{
  if (string == nullptr) {
    return {};
  }
  char buffer[256] = {};
  if (!CFStringGetCString(string, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
    return {};
  }
  return buffer;
}

std::string sourceId(TISInputSourceRef source)
{
  return toStdString(static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID)));
}

std::vector<uint8_t> layoutData(TISInputSourceRef source)
{
  const auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
  if (data == nullptr) {
    return {};
  }
  const auto *bytes = CFDataGetBytePtr(data);
  return std::vector<uint8_t>(bytes, bytes + CFDataGetLength(data));
}

std::vector<std::string> languages(TISInputSourceRef source)
{
  std::vector<std::string> result;
  const auto list = static_cast<CFArrayRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceLanguages));
  for (CFIndex i = 0; list != nullptr && i < CFArrayGetCount(list); ++i) {
    const auto language = static_cast<CFStringRef>(CFArrayGetValueAtIndex(list, i));
    if (auto code = toStdString(language); !code.empty()) {
      result.push_back(std::move(code));
    }
  }
  return result;
}

AutoTISInputSourceRef findSource(const std::string &id, bool includeAllInstalled)
{
  CFStringRef cfId = CFStringCreateWithCString(nullptr, id.c_str(), kCFStringEncodingUTF8);
  if (cfId == nullptr) {
    return {nullptr, CFRelease};
  }
  const void *keys[] = {kTISPropertyInputSourceID};
  const void *values[] = {cfId};
  AutoCFDictionary filter(
      CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks),
      CFRelease
  );
  CFRelease(cfId);
  if (!filter) {
    return {nullptr, CFRelease};
  }
  AutoCFArray sources(TISCreateInputSourceList(filter.get(), includeAllInstalled), CFRelease);
  if (!sources || CFArrayGetCount(sources.get()) == 0) {
    return {nullptr, CFRelease};
  }
  auto source = (TISInputSourceRef)CFArrayGetValueAtIndex(sources.get(), 0);
  CFRetain(source);
  return {source, CFRelease};
}

//! 'uchr' data giving Windows characters for the layout \p id (the PC variant need not be enabled)
std::vector<uint8_t> windowsTextLayout(const std::string &id, TISInputSourceRef current)
{
  if (id.empty() || current == nullptr) {
    return {};
  }
  if (isPcLayout(id)) {
    return layoutData(current);
  }
  for (const auto &candidate : pcLayoutCandidates(id)) {
    if (const auto source = findSource(candidate, true); source) {
      return layoutData(source.get());
    }
  }
  return {};
}

void fillCurrent(InputSourceSnapshot &snapshot, const InputSourceSnapshot &previous)
{
  AutoTISInputSourceRef keyboardLayout(TISCopyCurrentKeyboardLayoutInputSource(), CFRelease);
  AutoTISInputSourceRef inputSource(TISCopyCurrentKeyboardInputSource(), CFRelease);

  // key translation uses the current layout, unless an input method without
  // key data is active whose layout cannot be selected (then the ASCII layout)
  const bool inputSourceHasLayout =
      inputSource && TISGetInputSourceProperty(inputSource.get(), kTISPropertyUnicodeKeyLayoutData) != nullptr;
  CFBooleanRef isSelectCapable = nullptr;
  if (keyboardLayout) {
    isSelectCapable =
        (CFBooleanRef)TISGetInputSourceProperty(keyboardLayout.get(), kTISPropertyInputSourceIsSelectCapable);
  }
  const bool keyboardLayoutIsSelectCapable = isSelectCapable && CFBooleanGetValue(isSelectCapable);

  TISInputSourceRef translation = keyboardLayout.get();
  AutoTISInputSourceRef asciiKeyboardLayout(nullptr, CFRelease);
  if (inputSource && keyboardLayout && !inputSourceHasLayout && !keyboardLayoutIsSelectCapable) {
    asciiKeyboardLayout.reset(TISCopyCurrentASCIICapableKeyboardLayoutInputSource());
    if (asciiKeyboardLayout) {
      translation = asciiKeyboardLayout.get();
    }
  }

  snapshot.hasTranslationSource = translation != nullptr;
  if (translation != nullptr) {
    snapshot.translationLayout = layoutData(translation);
  }
  snapshot.keyboardType = LMGetKbdType();
  snapshot.currentLayoutId = keyboardLayout ? sourceId(keyboardLayout.get()) : std::string();
  if (inputSource) {
    snapshot.currentLanguages = languages(inputSource.get());
  }

  if (!snapshot.currentLayoutId.empty() && snapshot.currentLayoutId == previous.currentLayoutId) {
    snapshot.windowsTextLayout = previous.windowsTextLayout;
  } else {
    snapshot.windowsTextLayout = windowsTextLayout(snapshot.currentLayoutId, keyboardLayout.get());
  }
}

std::shared_ptr<const std::vector<InputSourceGroup>> readGroups()
{
  auto groups = std::make_shared<std::vector<InputSourceGroup>>();

  CFStringRef keys[] = {kTISPropertyInputSourceCategory};
  CFStringRef values[] = {kTISCategoryKeyboardInputSource};
  AutoCFDictionary filter(
      CFDictionaryCreate(nullptr, (const void **)keys, (const void **)values, 1, nullptr, nullptr), CFRelease
  );
  AutoCFArray sources(TISCreateInputSourceList(filter.get(), false), CFRelease);
  for (CFIndex i = 0; sources && i < CFArrayGetCount(sources.get()); ++i) {
    auto source = (TISInputSourceRef)CFArrayGetValueAtIndex(sources.get(), i);
    groups->push_back({sourceId(source), layoutData(source), languages(source)});
  }
  return groups;
}

void onInputSourcesChanged(CFNotificationCenterRef, void *, CFNotificationName name, const void *, CFDictionaryRef)
{
  const bool enabledChanged = name != nullptr && CFEqual(name, kTISNotifyEnabledKeyboardInputSourcesChanged);
  // distributed notifications arrive on the main run loop; hop there anyway if not
  runOnMainThreadAsync([enabledChanged] {
    auto &cache = InputSourceCache::instance();
    if (enabledChanged) {
      cache.refreshAllOnMain();
    } else {
      cache.refreshCurrentOnMain();
    }
  });
}

} // namespace

InputSourceCache &InputSourceCache::instance()
{
  // never destroyed: queued main-queue blocks and the notification observer may outlive static destruction
  static auto *cache = new InputSourceCache(); // NOSONAR - intentionally leaked singleton
  return *cache;
}

std::shared_ptr<const InputSourceSnapshot> InputSourceCache::snapshot()
{
  ensureStarted();
  if (isMainThread()) {
    refreshCurrentOnMain();
  }
  return load();
}

std::shared_ptr<const InputSourceSnapshot> InputSourceCache::refreshAndWait()
{
  ensureStarted();
  runOnMainThreadAndWait([] { InputSourceCache::instance().refreshAllOnMain(); });
  return load();
}

void InputSourceCache::ensureStarted()
{
  if (m_started.load()) {
    return;
  }
  if (isMainThread()) {
    startOnMain();
    return;
  }
  // only the first caller waits; a start that timed out still runs once the main queue is serviced
  if (!m_startQueued.exchange(true)) {
    runOnMainThreadAndWait([] { InputSourceCache::instance().startOnMain(); });
  }
}

void InputSourceCache::startOnMain()
{
  if (!requireMainThread(__func__) || m_started.load()) {
    return;
  }
  auto center = CFNotificationCenterGetDistributedCenter();
  CFNotificationCenterAddObserver(
      center, this, &onInputSourcesChanged, kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
      CFNotificationSuspensionBehaviorDeliverImmediately
  );
  CFNotificationCenterAddObserver(
      center, this, &onInputSourcesChanged, kTISNotifyEnabledKeyboardInputSourcesChanged, nullptr,
      CFNotificationSuspensionBehaviorDeliverImmediately
  );
  refreshAllOnMain();
  m_started.store(true);
  LOG_DEBUG("input source cache started, current layout: %s", load()->currentLayoutId.c_str());
}

void InputSourceCache::refreshCurrentOnMain()
{
  if (!requireMainThread(__func__)) {
    return;
  }
  const auto previous = load();
  auto next = std::make_shared<InputSourceSnapshot>();
  next->groups = previous->groups;
  fillCurrent(*next, *previous);
  store(std::move(next));
}

void InputSourceCache::refreshAllOnMain()
{
  if (!requireMainThread(__func__)) {
    return;
  }
  const auto previous = load();
  auto next = std::make_shared<InputSourceSnapshot>();
  next->groups = readGroups();
  fillCurrent(*next, *previous);
  store(std::move(next));
}

bool InputSourceCache::selectInputSourceOnMain(const std::string &id)
{
  if (!requireMainThread(__func__)) {
    return false;
  }
  const auto target = findSource(id, false);
  if (!target) {
    LOG_WARN("needed keyboard layout is null");
    return false;
  }
  CFBooleanRef canBeSet = nullptr;
  if (AutoTISInputSourceRef current(TISCopyCurrentKeyboardInputSource(), CFRelease); current) {
    canBeSet = (CFBooleanRef)TISGetInputSourceProperty(current.get(), kTISPropertyInputSourceIsEnableCapable);
  }
  if (!canBeSet) {
    LOG_WARN("needed keyboard layout is disabled for programmatically selection");
    return false;
  }
  if (TISSelectInputSource(target.get()) != noErr) {
    LOG_WARN("failed to set needed keyboard layout");
  }
  refreshCurrentOnMain();
  return true;
}

void InputSourceCache::selectNextInputSourceOnMain()
{
  if (!requireMainThread(__func__)) {
    return;
  }
  // enabled keyboard layouts and input modes the user can switch to, in system order
  const void *keys[] = {kTISPropertyInputSourceCategory, kTISPropertyInputSourceIsSelectCapable};
  const void *values[] = {kTISCategoryKeyboardInputSource, kCFBooleanTrue};
  AutoCFDictionary filter(
      CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks),
      CFRelease
  );
  if (!filter) {
    return;
  }

  AutoCFArray sources(TISCreateInputSourceList(filter.get(), false), CFRelease);
  AutoTISInputSourceRef current(TISCopyCurrentKeyboardInputSource(), CFRelease);
  // bounded: a Mac has a handful of input sources
  constexpr CFIndex kMaxSources = 64;
  const CFIndex count = sources ? std::min(CFArrayGetCount(sources.get()), kMaxSources) : 0;
  if (count < 2 || !current) {
    LOG_DEBUG("input source switch: nothing to switch to");
    return;
  }

  CFIndex currentIndex = -1;
  const auto currentId = static_cast<CFStringRef>(TISGetInputSourceProperty(current.get(), kTISPropertyInputSourceID));
  for (CFIndex i = 0; i < count && currentId != nullptr; ++i) {
    auto source = static_cast<TISInputSourceRef>(const_cast<void *>(CFArrayGetValueAtIndex(sources.get(), i)));
    const auto id = static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
    if (id != nullptr && CFEqual(id, currentId)) {
      currentIndex = i;
      break;
    }
  }

  const CFIndex next = (currentIndex + 1) % count;
  auto target = static_cast<TISInputSourceRef>(const_cast<void *>(CFArrayGetValueAtIndex(sources.get(), next)));
  if (TISSelectInputSource(target) != noErr) {
    LOG_WARN("failed to switch input source");
  } else {
    LOG_DEBUG("input source switched (%d of %d)", static_cast<int>(next), static_cast<int>(count));
  }
  refreshCurrentOnMain();
}

void InputSourceCache::store(std::shared_ptr<const InputSourceSnapshot> snapshot)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_snapshot = std::move(snapshot);
}

std::shared_ptr<const InputSourceSnapshot> InputSourceCache::load() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_snapshot;
}

} // namespace deskflow::osx

#pragma clang diagnostic pop

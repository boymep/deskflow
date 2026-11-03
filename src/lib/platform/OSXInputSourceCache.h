/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace deskflow::osx {

//! One enabled keyboard input source, in system order.
struct InputSourceGroup
{
  std::string id;
  std::vector<uint8_t> layout; //!< 'uchr' key layout data, empty if the source has none
  std::vector<std::string> languages;
};

/**
 * @brief Plain-data copy of the text input source state.
 *
 * Everything is copied out of HIToolbox on the main thread, so any thread can
 * read a snapshot (no TIS objects, no CF lifetimes tied to the main thread).
 */
struct InputSourceSnapshot
{
  bool hasTranslationSource = false;      //!< a keyboard layout to translate local key events exists
  std::vector<uint8_t> translationLayout; //!< its 'uchr' data (may be empty)
  uint32_t keyboardType = 0;              //!< LMGetKbdType()
  std::string currentLayoutId;            //!< id of the current keyboard layout
  std::vector<uint8_t> windowsTextLayout; //!< 'uchr' data of the PC variant of the current layout, empty if none
  std::vector<std::string> currentLanguages; //!< languages of the current keyboard input source
  std::shared_ptr<const std::vector<InputSourceGroup>> groups = std::make_shared<std::vector<InputSourceGroup>>();
};

/**
 * @brief Process-wide cache of the text input source state (macOS).
 *
 * HIToolbox text input source calls must run on the main queue; the core runs
 * its network loop and the event tap on other threads. The cache is filled on
 * the main thread at start and on the distributed notifications
 * kTISNotifySelectedKeyboardInputSourceChanged and
 * kTISNotifyEnabledKeyboardInputSourcesChanged; other threads only read it.
 */
class InputSourceCache
{
public:
  static InputSourceCache &instance();

  //! Current snapshot, any thread. On the main thread the current source is re-read first.
  std::shared_ptr<const InputSourceSnapshot> snapshot();

  //! Re-reads everything on the main thread (waits a bounded time off main), then returns the snapshot.
  std::shared_ptr<const InputSourceSnapshot> refreshAndWait();

  //! Main thread only. Selects the enabled source \p id; returns false if not selectable or the call failed.
  bool selectInputSourceOnMain(const std::string &id);

  //! Main thread only. Selects the input source after the current one (wrapping), like the system shortcut.
  void selectNextInputSourceOnMain();

  //! Main thread only. Re-reads the current source (cheap, used on selection changes).
  void refreshCurrentOnMain();

  //! Main thread only. Re-reads the enabled sources and the current source.
  void refreshAllOnMain();

private:
  InputSourceCache() = default;
  void ensureStarted();
  void startOnMain();
  void store(std::shared_ptr<const InputSourceSnapshot> snapshot);
  std::shared_ptr<const InputSourceSnapshot> load() const;

  mutable std::mutex m_mutex;
  std::shared_ptr<const InputSourceSnapshot> m_snapshot = std::make_shared<InputSourceSnapshot>();
  std::atomic<bool> m_started{false};
  std::atomic<bool> m_startQueued{false};
};

} // namespace deskflow::osx

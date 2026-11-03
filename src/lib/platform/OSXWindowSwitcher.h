/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXWindowsKeyboard.h"

#include <memory>

namespace deskflow::osx {

//! Built-in Windows-style window switcher of the macOS client (AltTabMode::Builtin).
/*!
Lives in the client process. deskflow-core runs QApplication (and with it
NSApplication) on the main thread while key events are handled on the core
thread, so handle() may be called from any thread: every command is queued, in
order, to the main dispatch queue, where all AppKit and Accessibility work runs.

Windows are listed via Accessibility (needs the same permission the client
already needs to post events). Windows on other Spaces are usually not
reported by Accessibility and are then missing from the list.
*/
class OSXWindowSwitcher
{
public:
  OSXWindowSwitcher();
  ~OSXWindowSwitcher();
  OSXWindowSwitcher(const OSXWindowSwitcher &) = delete;
  OSXWindowSwitcher &operator=(const OSXWindowSwitcher &) = delete;

  //! Queues \p command for the main thread. Thread safe.
  void handle(SwitcherCommand command);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::osx

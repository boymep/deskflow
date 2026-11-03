/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/unix/AppUtilUnix.h"

#include "base/Log.h"
#include "common/PlatformInfo.h"

#if WINAPI_XWINDOWS
#include <X11/XKBlib.h>
#include <deskflow/unix/XkbLayoutsParser.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <platform/OSXInputSourceCache.h>
#endif

AppUtilUnix::AppUtilUnix(const IEventQueue *)
{
  // do nothing
}

int startStatic()
{
  return AppUtil::instance().app().start();
}

int AppUtilUnix::run()
{
  return app().runInner(&startStatic);
}

std::vector<std::string> AppUtilUnix::getKeyboardLayoutList()
{
  std::vector<std::string> layoutLangCodes;

#if WINAPI_XWINDOWS
  layoutLangCodes = XkbLayoutsParser::getXkbLanguageList();

#elif defined(Q_OS_MACOS)
  // input sources are read on the main thread (TIS asserts it runs there)
  const auto inputSources = deskflow::osx::InputSourceCache::instance().refreshAndWait();
  for (const auto &group : *inputSources->groups) {
    // save only the first language code
    if (group.languages.empty()) {
      continue;
    }
    const auto &langCode = group.languages.front();
    if (langCode.size() == 2 &&
        std::find(layoutLangCodes.begin(), layoutLangCodes.end(), langCode) == layoutLangCodes.end()) {
      layoutLangCodes.push_back(langCode);
    }
  }
#endif

  return layoutLangCodes;
}

// TODO: read the layout for x and wayland via xkbcommon
std::string AppUtilUnix::getCurrentLanguageCode()
{
  std::string result = "";
  if (deskflow::platform::isWayland())
    return result;

#if WINAPI_XWINDOWS

  auto display = XOpenDisplay(nullptr);
  if (!display) {
    LOG_WARN("failed to open x11 default display");
    return result;
  }

  auto kbdDescr = XkbAllocKeyboard();
  if (!kbdDescr) {
    LOG_WARN("failed to get x11 keyboard description");
    return result;
  }
  XkbGetNames(display, XkbSymbolsNameMask, kbdDescr);

  Atom symNameAtom = kbdDescr->names->symbols;
  auto rawLayouts = std::string(XGetAtomName(display, symNameAtom));

  XkbStateRec state;
  XkbGetState(display, XkbUseCoreKbd, &state);
  auto neededGroupIndex = static_cast<int>(state.group);

  size_t groupIdx = 0;
  size_t groupStartI = 0;
  for (size_t strI = 0; strI < rawLayouts.size(); strI++) {
    if (rawLayouts[strI] != '+') {
      continue;
    }

    if (auto group = rawLayouts.substr(groupStartI, strI - groupStartI);
        group.find("group", 0, 5) == std::string::npos && group.find("inet", 0, 4) == std::string::npos &&
        group.find("pc", 0, 2) == std::string::npos) {
      if (neededGroupIndex == groupIdx) {
        result = group.substr(0, std::min(group.find('(', 0), group.find(':', 0)));
        break;
      }
      groupIdx++;
    }

    groupStartI = strI + 1;
  }

  XkbFreeNames(kbdDescr, XkbSymbolsNameMask, true);
  XFree(kbdDescr);
  XCloseDisplay(display);

  result = XkbLayoutsParser::convertLayoutToISO(result);

#elif defined(Q_OS_MACOS)
  // called for every key on the server's core thread: read the cache, TIS is main thread only
  const auto inputSources = deskflow::osx::InputSourceCache::instance().snapshot();
  if (!inputSources->currentLanguages.empty()) {
    result = inputSources->currentLanguages.front();
  }
#endif
  return result;
}

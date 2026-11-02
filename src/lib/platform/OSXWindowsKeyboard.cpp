/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXWindowsKeyboard.h"

#include <Carbon/Carbon.h>

namespace deskflow::osx {

namespace {

constexpr WindowsScancode kScancodeMask = 0x1ff;
constexpr WindowsScancode kExtended = 0x100;

struct ScancodeEntry
{
  WindowsScancode scancode;
  MacVirtualKey virtualKey;
};

// Windows set-1 scancodes (as sent by the Windows server, 0x100 = extended)
// and the macOS virtual key at the same physical position.
constexpr ScancodeEntry kScancodeTable[] = {
    {0x01, kVK_Escape},
    {0x02, kVK_ANSI_1},
    {0x03, kVK_ANSI_2},
    {0x04, kVK_ANSI_3},
    {0x05, kVK_ANSI_4},
    {0x06, kVK_ANSI_5},
    {0x07, kVK_ANSI_6},
    {0x08, kVK_ANSI_7},
    {0x09, kVK_ANSI_8},
    {0x0a, kVK_ANSI_9},
    {0x0b, kVK_ANSI_0},
    {0x0c, kVK_ANSI_Minus},
    {0x0d, kVK_ANSI_Equal},
    {0x0e, kVK_Delete}, // Backspace
    {0x0f, kVK_Tab},
    {0x10, kVK_ANSI_Q},
    {0x11, kVK_ANSI_W},
    {0x12, kVK_ANSI_E},
    {0x13, kVK_ANSI_R},
    {0x14, kVK_ANSI_T},
    {0x15, kVK_ANSI_Y},
    {0x16, kVK_ANSI_U},
    {0x17, kVK_ANSI_I},
    {0x18, kVK_ANSI_O},
    {0x19, kVK_ANSI_P},
    {0x1a, kVK_ANSI_LeftBracket},
    {0x1b, kVK_ANSI_RightBracket},
    {0x1c, kVK_Return},
    {0x1d, kVK_Control}, // Left Ctrl
    {0x1e, kVK_ANSI_A},
    {0x1f, kVK_ANSI_S},
    {0x20, kVK_ANSI_D},
    {0x21, kVK_ANSI_F},
    {0x22, kVK_ANSI_G},
    {0x23, kVK_ANSI_H},
    {0x24, kVK_ANSI_J},
    {0x25, kVK_ANSI_K},
    {0x26, kVK_ANSI_L},
    {0x27, kVK_ANSI_Semicolon},
    {0x28, kVK_ANSI_Quote},
    {0x29, kVK_ANSI_Grave}, // key left of 1 (` ~ / ё Ё)
    {0x2a, kVK_Shift},
    {0x2b, kVK_ANSI_Backslash},
    {0x2c, kVK_ANSI_Z},
    {0x2d, kVK_ANSI_X},
    {0x2e, kVK_ANSI_C},
    {0x2f, kVK_ANSI_V},
    {0x30, kVK_ANSI_B},
    {0x31, kVK_ANSI_N},
    {0x32, kVK_ANSI_M},
    {0x33, kVK_ANSI_Comma},
    {0x34, kVK_ANSI_Period},
    {0x35, kVK_ANSI_Slash},
    {0x36, kVK_RightShift},
    {0x37, kVK_ANSI_KeypadMultiply},
    {0x38, kVK_Option}, // Left Alt
    {0x39, kVK_Space},
    {0x3b, kVK_F1},
    {0x3c, kVK_F2},
    {0x3d, kVK_F3},
    {0x3e, kVK_F4},
    {0x3f, kVK_F5},
    {0x40, kVK_F6},
    {0x41, kVK_F7},
    {0x42, kVK_F8},
    {0x43, kVK_F9},
    {0x44, kVK_F10},
    {0x47, kVK_ANSI_Keypad7},
    {0x48, kVK_ANSI_Keypad8},
    {0x49, kVK_ANSI_Keypad9},
    {0x4a, kVK_ANSI_KeypadMinus},
    {0x4b, kVK_ANSI_Keypad4},
    {0x4c, kVK_ANSI_Keypad5},
    {0x4d, kVK_ANSI_Keypad6},
    {0x4e, kVK_ANSI_KeypadPlus},
    {0x4f, kVK_ANSI_Keypad1},
    {0x50, kVK_ANSI_Keypad2},
    {0x51, kVK_ANSI_Keypad3},
    {0x52, kVK_ANSI_Keypad0},
    {0x53, kVK_ANSI_KeypadDecimal},
    {0x56, kVK_ISO_Section}, // ISO key between left Shift and Z
    {0x57, kVK_F11},
    {0x58, kVK_F12},
    {0x59, kVK_ANSI_KeypadEquals},
    {0x64, kVK_F13},
    {0x65, kVK_F14},
    {0x66, kVK_F15},
    {0x67, kVK_F16},
    {0x68, kVK_F17},
    {0x69, kVK_F18},
    {0x6a, kVK_F19},
    {0x6b, kVK_F20},
    {kExtended | 0x1c, kVK_ANSI_KeypadEnter},
    {kExtended | 0x1d, kVK_RightControl},
    {kExtended | 0x35, kVK_ANSI_KeypadDivide},
    {kExtended | 0x38, kVK_RightOption}, // Right Alt
    {kExtended | 0x47, kVK_Home},
    {kExtended | 0x48, kVK_UpArrow},
    {kExtended | 0x49, kVK_PageUp},
    {kExtended | 0x4b, kVK_LeftArrow},
    {kExtended | 0x4d, kVK_RightArrow},
    {kExtended | 0x4f, kVK_End},
    {kExtended | 0x50, kVK_DownArrow},
    {kExtended | 0x51, kVK_PageDown},
    {kExtended | 0x52, kVK_Help}, // Insert
    {kExtended | 0x53, kVK_ForwardDelete},
    {kExtended | 0x5b, kVK_Command},      // Left Windows
    {kExtended | 0x5c, kVK_RightCommand}, // Right Windows
    {kExtended | 0x5d, kVK_ContextualMenu},
};

constexpr std::array<MacModifier, 8> kAllMacModifiers = {kMacShiftL,   kMacShiftR,  kMacControlL, kMacControlR,
                                                         kMacOptionL,  kMacOptionR, kMacCommandL, kMacCommandR};

constexpr RemoteModifiers kRemoteShift = kRemoteShiftL | kRemoteShiftR;
constexpr RemoteModifiers kRemoteCtrl = kRemoteCtrlL | kRemoteCtrlR;
constexpr RemoteModifiers kRemoteAlt = kRemoteAltL | kRemoteAltR;
constexpr RemoteModifiers kRemoteWin = kRemoteWinL | kRemoteWinR;

bool endsWith(const std::string &s, const std::string &suffix)
{
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Shift keys held on the server, as Mac shift keys on the same side.
MacModifiers macShift(RemoteModifiers remote)
{
  MacModifiers mods = 0;
  if (remote & kRemoteShiftL)
    mods |= kMacShiftL;
  if (remote & kRemoteShiftR)
    mods |= kMacShiftR;
  return mods;
}

} // namespace

std::optional<MacVirtualKey> windowsScancodeToMacVirtualKey(WindowsScancode scancode)
{
  scancode &= kScancodeMask;
  for (const auto &entry : kScancodeTable) {
    if (entry.scancode == scancode) {
      return entry.virtualKey;
    }
  }
  return std::nullopt;
}

bool isMacTypingKey(MacVirtualKey virtualKey)
{
  // kVK_ANSI_A (0x00) .. kVK_ANSI_Period (0x2f) are the letter, digit and
  // punctuation keys (incl. kVK_ISO_Section 0x0a), except Return (0x24).
  return (virtualKey <= kVK_ANSI_Period && virtualKey != kVK_Return) || virtualKey == kVK_ANSI_Grave;
}

bool isPcLayout(const std::string &inputSourceId)
{
  return endsWith(inputSourceId, "-PC") || endsWith(inputSourceId, "PC") || endsWith(inputSourceId, "Win");
}

std::vector<std::string> pcLayoutCandidates(const std::string &inputSourceId)
{
  if (inputSourceId.empty() || isPcLayout(inputSourceId)) {
    return {};
  }
  // Apple ships e.g. RussianWin, Ukrainian-PC, British-PC, ArabicPC.
  return {inputSourceId + "Win", inputSourceId + "-PC", inputSourceId + "PC"};
}

RemoteModifier remoteModifierForScancode(WindowsScancode scancode)
{
  switch (scancode & kScancodeMask) {
  case 0x2a:
    return kRemoteShiftL;
  case 0x36:
    return kRemoteShiftR;
  case 0x1d:
    return kRemoteCtrlL;
  case kExtended | 0x1d:
    return kRemoteCtrlR;
  case 0x38:
    return kRemoteAltL;
  case kExtended | 0x38:
    return kRemoteAltR;
  case kExtended | 0x5b:
    return kRemoteWinL;
  case kExtended | 0x5c:
    return kRemoteWinR;
  default:
    return static_cast<RemoteModifier>(0);
  }
}

MacVirtualKey macVirtualKeyForModifier(MacModifier modifier)
{
  switch (modifier) {
  case kMacShiftL:
    return kVK_Shift;
  case kMacShiftR:
    return kVK_RightShift;
  case kMacControlL:
    return kVK_Control;
  case kMacControlR:
    return kVK_RightControl;
  case kMacOptionL:
    return kVK_Option;
  case kMacOptionR:
    return kVK_RightOption;
  case kMacCommandL:
    return kVK_Command;
  case kMacCommandR:
    return kVK_RightCommand;
  }
  return kVK_Shift;
}

MacModifiers baseMacModifiers(RemoteModifiers remote, bool swapCtrlCmd)
{
  MacModifiers mods = macShift(remote);
  const MacModifier ctrlL = swapCtrlCmd ? kMacCommandL : kMacControlL;
  const MacModifier ctrlR = swapCtrlCmd ? kMacCommandR : kMacControlR;
  const MacModifier winL = swapCtrlCmd ? kMacControlL : kMacCommandL;
  const MacModifier winR = swapCtrlCmd ? kMacControlR : kMacCommandR;
  if (remote & kRemoteCtrlL)
    mods |= ctrlL;
  if (remote & kRemoteCtrlR)
    mods |= ctrlR;
  if (remote & kRemoteWinL)
    mods |= winL;
  if (remote & kRemoteWinR)
    mods |= winR;
  if (remote & kRemoteAltL)
    mods |= kMacOptionL;
  if (remote & kRemoteAltR)
    mods |= kMacOptionR;
  return mods;
}

KeyDecision decideKey(WindowsScancode, MacVirtualKey virtualKey, RemoteModifiers remote, bool swapCtrlCmd)
{
  KeyDecision d;
  d.virtualKey = virtualKey;
  d.modifiers = baseMacModifiers(remote, swapCtrlCmd);

  const bool ctrl = (remote & kRemoteCtrl) != 0;
  const bool otherMods = (remote & (kRemoteAlt | kRemoteWin)) != 0;
  const MacModifiers shift = macShift(remote);

  // Windows editing keys and Ctrl+Tab; only with Ctrl and/or Shift held so that
  // e.g. Ctrl+Alt+Left keeps its plain mapping.
  if (!otherMods) {
    auto remap = [&d](MacVirtualKey key, MacModifiers mods, bool sticky = false) {
      d.virtualKey = key;
      d.modifiers = mods;
      d.sticky = sticky;
    };
    switch (virtualKey) {
    case kVK_Tab: // Ctrl(+Shift)+Tab: tab switching on both systems
      if (ctrl)
        remap(kVK_Tab, kMacControlL | shift, true);
      break;
    case kVK_Delete: // Ctrl+Backspace: delete word left
    case kVK_ForwardDelete: // Ctrl+Delete: delete word right
    case kVK_LeftArrow: // Ctrl+Left/Right: word jump (Shift selects)
    case kVK_RightArrow:
      if (ctrl)
        remap(virtualKey, kMacOptionL | shift);
      break;
    case kVK_Home: // Home: line start, Ctrl+Home: document start
      remap(ctrl ? kVK_UpArrow : kVK_LeftArrow, kMacCommandL | shift);
      break;
    case kVK_End: // End: line end, Ctrl+End: document end
      remap(ctrl ? kVK_DownArrow : kVK_RightArrow, kMacCommandL | shift);
      break;
    case kVK_ANSI_Y: // Ctrl+Y: redo
      if (ctrl)
        remap(kVK_ANSI_Z, kMacCommandL | kMacShiftL);
      break;
    default:
      break;
    }
  }

  // Plain typing (Shift allowed): the platform may attach the Windows character.
  if (d.virtualKey == virtualKey && (remote & ~kRemoteShift) == 0 && isMacTypingKey(virtualKey)) {
    d.allowText = true;
    // the ISO key types the same as the backslash key in Windows US/RU layouts;
    // macOS layouts treat kVK_ISO_Section inconsistently, so translate backslash.
    d.textKey = (virtualKey == kVK_ISO_Section) ? static_cast<MacVirtualKey>(kVK_ANSI_Backslash) : virtualKey;
  }
  return d;
}

WindowsKeyboardMapper::WindowsKeyboardMapper(bool swapCtrlCmd) : m_swapCtrlCmd(swapCtrlCmd)
{
  // nothing else
}

bool WindowsKeyboardMapper::keyDown(WindowsScancode button, KeyActions &out)
{
  const WindowsScancode scancode = button & kScancodeMask;

  if (const auto modifier = remoteModifierForScancode(scancode); modifier != 0) {
    if ((m_remote & modifier) == 0) {
      m_remote |= modifier;
      m_pressed[scancode].down = true;
      modifierChanged(out);
    }
    // Windows auto-repeats held modifiers; nothing to post for that
    return true;
  }

  const auto virtualKey = windowsScancodeToMacVirtualKey(scancode);
  if (!virtualKey.has_value()) {
    return false;
  }

  if (m_pressed[scancode].down) {
    // mis-reported auto-repeat
    return keyRepeat(scancode, 1, out);
  }

  const auto decision = decideKey(scancode, *virtualKey, m_remote, m_swapCtrlCmd);
  reconcile(decision.modifiers, out);
  out.push_back(keyAction(decision, true));

  auto &pressed = m_pressed[scancode];
  pressed.down = true;
  pressed.virtualKey = decision.virtualKey;
  pressed.text = decision.allowText;
  pressed.textKey = decision.textKey;
  // another key ends a sticky chord (Ctrl+Tab, then Ctrl+W is Command+W again)
  m_sticky = decision.sticky ? scancode : 0;
  return true;
}

bool WindowsKeyboardMapper::keyRepeat(WindowsScancode button, int32_t count, KeyActions &out)
{
  const WindowsScancode scancode = button & kScancodeMask;
  if (remoteModifierForScancode(scancode) != 0) {
    return m_pressed[scancode].down;
  }

  auto &pressed = m_pressed[scancode];
  const auto virtualKey = windowsScancodeToMacVirtualKey(scancode);
  if (!pressed.down || !virtualKey.has_value()) {
    return false;
  }

  // modifiers may have changed since the key went down, so decide again
  const auto decision = decideKey(scancode, *virtualKey, m_remote, m_swapCtrlCmd);
  if (decision.virtualKey != pressed.virtualKey) {
    // e.g. Home held, then Ctrl pressed: Left -> Up; release the old key first
    KeyAction up;
    up.virtualKey = pressed.virtualKey;
    up.down = false;
    up.text = pressed.text;
    up.textKey = pressed.textKey;
    up.shift = (m_remote & kRemoteShift) != 0;
    out.push_back(up);
    pressed.virtualKey = decision.virtualKey;
  }
  pressed.text = decision.allowText;
  pressed.textKey = decision.textKey;
  if (decision.sticky) {
    m_sticky = scancode;
  }

  reconcile(decision.modifiers, out);
  auto action = keyAction(decision, true);
  action.repeat = true;
  action.count = count < 1 ? 1 : count;
  out.push_back(action);
  return true;
}

bool WindowsKeyboardMapper::keyUp(WindowsScancode button, KeyActions &out)
{
  const WindowsScancode scancode = button & kScancodeMask;
  auto &pressed = m_pressed[scancode];
  if (!pressed.down) {
    return false;
  }

  if (const auto modifier = remoteModifierForScancode(scancode); modifier != 0) {
    pressed = PressedKey{};
    m_remote &= static_cast<RemoteModifiers>(~modifier);
    modifierChanged(out);
    return true;
  }

  KeyAction up;
  up.virtualKey = pressed.virtualKey;
  up.down = false;
  up.text = pressed.text;
  up.textKey = pressed.textKey;
  up.shift = (m_remote & kRemoteShift) != 0;
  out.push_back(up);
  pressed = PressedKey{};

  reconcile(restModifiers(), out);
  return true;
}

void WindowsKeyboardMapper::allKeysUp(KeyActions &out)
{
  for (size_t scancode = 0; scancode < kNumScancodes; ++scancode) {
    auto &pressed = m_pressed[scancode];
    if (pressed.down && remoteModifierForScancode(static_cast<WindowsScancode>(scancode)) == 0) {
      KeyAction up;
      up.virtualKey = pressed.virtualKey;
      up.down = false;
      up.text = pressed.text;
      up.textKey = pressed.textKey;
      out.push_back(up);
    }
    pressed = PressedKey{};
  }
  m_remote = 0;
  m_sticky = 0;
  reconcile(0, out);
}

void WindowsKeyboardMapper::modifierChanged(KeyActions &out)
{
  // Extension point (next slice): remote modifier chords such as Ctrl+Shift
  // (input source switch) are detected here from m_remote transitions.
  reconcile(restModifiers(), out);
}

MacModifiers WindowsKeyboardMapper::restModifiers()
{
  if (m_sticky != 0) {
    const auto virtualKey = windowsScancodeToMacVirtualKey(m_sticky);
    if (virtualKey.has_value()) {
      const auto decision = decideKey(m_sticky, *virtualKey, m_remote, m_swapCtrlCmd);
      if (decision.sticky) {
        return decision.modifiers;
      }
    }
    m_sticky = 0;
  }
  return baseMacModifiers(m_remote, m_swapCtrlCmd);
}

void WindowsKeyboardMapper::reconcile(MacModifiers target, KeyActions &out)
{
  const auto release = static_cast<MacModifiers>(m_posted & ~target);
  const auto press = static_cast<MacModifiers>(target & ~m_posted);
  for (const auto modifier : kAllMacModifiers) {
    if (release & modifier) {
      KeyAction action;
      action.virtualKey = macVirtualKeyForModifier(modifier);
      action.down = false;
      action.isModifier = true;
      out.push_back(action);
    }
  }
  for (const auto modifier : kAllMacModifiers) {
    if (press & modifier) {
      KeyAction action;
      action.virtualKey = macVirtualKeyForModifier(modifier);
      action.down = true;
      action.isModifier = true;
      out.push_back(action);
    }
  }
  m_posted = target;
}

KeyAction WindowsKeyboardMapper::keyAction(const KeyDecision &decision, bool down) const
{
  KeyAction action;
  action.virtualKey = decision.virtualKey;
  action.down = down;
  action.text = decision.allowText;
  action.textKey = decision.textKey;
  action.shift = (m_remote & kRemoteShift) != 0;
  return action;
}

} // namespace deskflow::osx

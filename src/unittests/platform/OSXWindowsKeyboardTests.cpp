/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXWindowsKeyboardTests.h"

#include "platform/OSXWindowsKeyboard.h"

#include <Carbon/Carbon.h>

using namespace deskflow::osx;

namespace {

constexpr WindowsScancode kScLCtrl = 0x1d;
constexpr WindowsScancode kScRCtrl = 0x11d;
constexpr WindowsScancode kScLShift = 0x2a;
constexpr WindowsScancode kScRShift = 0x36;
constexpr WindowsScancode kScT = 0x14;
constexpr WindowsScancode kScQ = 0x10;
constexpr WindowsScancode kScLeft = 0x14b;
constexpr WindowsScancode kScLAlt = 0x38;
constexpr WindowsScancode kScLWin = 0x15b;
constexpr WindowsScancode kScW = 0x11;
constexpr WindowsScancode kScY = 0x15;
constexpr WindowsScancode kScTab = 0x0f;
constexpr WindowsScancode kScBackspace = 0x0e;
constexpr WindowsScancode kScHome = 0x147;
constexpr WindowsScancode kScEnd = 0x14f;
constexpr WindowsScancode kScA = 0x1e;

// compact form of an action list: "+37" = modifier 0x37 down, "K0d" = key down, "k0d" = key up
QString describe(const KeyActions &actions)
{
  QStringList parts;
  for (const auto &a : actions) {
    if (a.switchInputSource) {
      parts << "S";
      continue;
    }
    QString prefix = a.isModifier ? (a.down ? "+" : "-") : (a.down ? (a.repeat ? "R" : "K") : "k");
    parts << prefix + QString::number(a.virtualKey, 16) + (a.text ? "t" : "");
  }
  return parts.join(' ');
}

} // namespace

void OSXWindowsKeyboardTests::scancodeTable()
{
  QCOMPARE(windowsScancodeToMacVirtualKey(0x1e), std::optional<MacVirtualKey>(kVK_ANSI_A));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x29), std::optional<MacVirtualKey>(kVK_ANSI_Grave));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x56), std::optional<MacVirtualKey>(kVK_ISO_Section));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x0e), std::optional<MacVirtualKey>(kVK_Delete));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x1c), std::optional<MacVirtualKey>(kVK_Return));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x11c), std::optional<MacVirtualKey>(kVK_ANSI_KeypadEnter));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x153), std::optional<MacVirtualKey>(kVK_ForwardDelete));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x53), std::optional<MacVirtualKey>(kVK_ANSI_KeypadDecimal));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x147), std::optional<MacVirtualKey>(kVK_Home));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x47), std::optional<MacVirtualKey>(kVK_ANSI_Keypad7));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x138), std::optional<MacVirtualKey>(kVK_RightOption));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x11d), std::optional<MacVirtualKey>(kVK_RightControl));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x15b), std::optional<MacVirtualKey>(kVK_Command));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x15c), std::optional<MacVirtualKey>(kVK_RightCommand));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x15d), std::optional<MacVirtualKey>(kVK_ContextualMenu));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x3b), std::optional<MacVirtualKey>(kVK_F1));
  QCOMPARE(windowsScancodeToMacVirtualKey(0x58), std::optional<MacVirtualKey>(kVK_F12));
  // bits above the 9-bit scancode are ignored
  QCOMPARE(windowsScancodeToMacVirtualKey(0xfe1e), std::optional<MacVirtualKey>(kVK_ANSI_A));

  // every mapped scancode yields a distinct virtual key
  QSet<MacVirtualKey> seen;
  int mapped = 0;
  for (WindowsScancode sc = 0; sc < 0x200; ++sc) {
    if (const auto vk = windowsScancodeToMacVirtualKey(sc)) {
      QVERIFY2(!seen.contains(*vk), qPrintable(QString::number(sc, 16)));
      seen.insert(*vk);
      ++mapped;
    }
  }
  QVERIFY(mapped >= 105);
}

void OSXWindowsKeyboardTests::scancodeTableUnmapped()
{
  QVERIFY(!windowsScancodeToMacVirtualKey(0x00).has_value());
  QVERIFY(!windowsScancodeToMacVirtualKey(0x3a).has_value());  // Caps Lock
  QVERIFY(!windowsScancodeToMacVirtualKey(0x45).has_value());  // Pause / Num Lock
  QVERIFY(!windowsScancodeToMacVirtualKey(0x46).has_value());  // Scroll Lock
  QVERIFY(!windowsScancodeToMacVirtualKey(0x137).has_value()); // Print Screen
  QVERIFY(!windowsScancodeToMacVirtualKey(0x120).has_value()); // media: mute
}

void OSXWindowsKeyboardTests::typingKeys()
{
  QVERIFY(isMacTypingKey(kVK_ANSI_A));
  QVERIFY(isMacTypingKey(kVK_ANSI_Grave));
  QVERIFY(isMacTypingKey(kVK_ISO_Section));
  QVERIFY(isMacTypingKey(kVK_ANSI_Period));
  QVERIFY(!isMacTypingKey(kVK_Return));
  QVERIFY(!isMacTypingKey(kVK_Tab));
  QVERIFY(!isMacTypingKey(kVK_Space));
  QVERIFY(!isMacTypingKey(kVK_Delete));
  QVERIFY(!isMacTypingKey(kVK_ANSI_Keypad1));
}

void OSXWindowsKeyboardTests::pcLayoutCandidates()
{
  const auto c = deskflow::osx::pcLayoutCandidates("com.apple.keylayout.Russian");
  QVERIFY(!c.empty());
  QCOMPARE(c.front(), std::string("com.apple.keylayout.RussianWin"));
  QVERIFY(deskflow::osx::pcLayoutCandidates("com.apple.keylayout.RussianWin").empty());
  QVERIFY(deskflow::osx::pcLayoutCandidates("com.apple.keylayout.British-PC").empty());
  QVERIFY(deskflow::osx::pcLayoutCandidates("").empty());
  QVERIFY(isPcLayout("com.apple.keylayout.ArabicPC"));
  QVERIFY(!isPcLayout("com.apple.keylayout.ABC"));
}

void OSXWindowsKeyboardTests::baseModifiers()
{
  QCOMPARE(baseMacModifiers(kRemoteCtrlL, true), MacModifiers(kMacCommandL));
  QCOMPARE(baseMacModifiers(kRemoteCtrlR, true), MacModifiers(kMacCommandR));
  QCOMPARE(baseMacModifiers(kRemoteWinL, true), MacModifiers(kMacControlL));
  QCOMPARE(baseMacModifiers(kRemoteAltR, true), MacModifiers(kMacOptionR));
  QCOMPARE(baseMacModifiers(kRemoteShiftR, true), MacModifiers(kMacShiftR));
  QCOMPARE(baseMacModifiers(kRemoteCtrlL, false), MacModifiers(kMacControlL));
  QCOMPARE(baseMacModifiers(kRemoteWinL, false), MacModifiers(kMacCommandL));
}

void OSXWindowsKeyboardTests::decideShortcutsAndText()
{
  // plain and shifted typing carries text
  auto d = decideKey(kScA, kVK_ANSI_A, 0, true);
  QVERIFY(d.allowText);
  QCOMPARE(d.textKey, MacVirtualKey(kVK_ANSI_A));
  d = decideKey(kScA, kVK_ANSI_A, kRemoteShiftL, true);
  QVERIFY(d.allowText);
  QCOMPARE(d.modifiers, MacModifiers(kMacShiftL));
  // ISO key translates like backslash
  d = decideKey(0x56, kVK_ISO_Section, 0, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_ISO_Section));
  QCOMPARE(d.textKey, MacVirtualKey(kVK_ANSI_Backslash));
  // Ctrl+W -> Command+W, physical key, no text
  d = decideKey(kScW, kVK_ANSI_W, kRemoteCtrlL, true);
  QVERIFY(!d.allowText);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_ANSI_W));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL));
  // Ctrl+Shift+4 -> Command+Shift+4
  d = decideKey(0x05, kVK_ANSI_4, kRemoteCtrlL | kRemoteShiftL, true);
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacShiftL));
  // Alt+key: Option, no text
  d = decideKey(kScA, kVK_ANSI_A, kRemoteAltL, true);
  QVERIFY(!d.allowText);
  QCOMPARE(d.modifiers, MacModifiers(kMacOptionL));
  // Return never carries text
  QVERIFY(!decideKey(0x1c, kVK_Return, 0, true).allowText);
}

void OSXWindowsKeyboardTests::decideEditingExceptions()
{
  auto d = decideKey(kScBackspace, kVK_Delete, kRemoteCtrlL, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_Delete));
  QCOMPARE(d.modifiers, MacModifiers(kMacOptionL));
  QVERIFY(!d.sticky);

  d = decideKey(0x153, kVK_ForwardDelete, kRemoteCtrlR, true);
  QCOMPARE(d.modifiers, MacModifiers(kMacOptionL));

  d = decideKey(0x14b, kVK_LeftArrow, kRemoteCtrlL | kRemoteShiftL, true);
  QCOMPARE(d.modifiers, MacModifiers(kMacOptionL | kMacShiftL));

  d = decideKey(kScHome, kVK_Home, 0, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_LeftArrow));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL));
  d = decideKey(kScEnd, kVK_End, kRemoteShiftR, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_RightArrow));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacShiftR));
  d = decideKey(kScHome, kVK_Home, kRemoteCtrlL, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_UpArrow));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL));
  d = decideKey(kScEnd, kVK_End, kRemoteCtrlL | kRemoteShiftL, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_DownArrow));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacShiftL));

  d = decideKey(kScY, kVK_ANSI_Y, kRemoteCtrlL, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_ANSI_Z));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacShiftL));

  d = decideKey(kScTab, kVK_Tab, kRemoteCtrlL | kRemoteShiftL, true);
  QCOMPARE(d.modifiers, MacModifiers(kMacControlL | kMacShiftL));
  QVERIFY(d.sticky);

  // with Alt or Win held, no exception applies
  d = decideKey(0x14b, kVK_LeftArrow, kRemoteCtrlL | kRemoteAltL, true);
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacOptionL));
  d = decideKey(kScHome, kVK_Home, kRemoteWinL, true);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_Home));
  // plain Tab and Y are untouched
  QCOMPARE(decideKey(kScTab, kVK_Tab, 0, true).modifiers, MacModifiers(0));
  QVERIFY(decideKey(kScY, kVK_ANSI_Y, 0, true).allowText);
}

void OSXWindowsKeyboardTests::mapperPlainShortcut()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  QVERIFY(m.keyDown(kScLCtrl, out));
  QCOMPARE(describe(out), QString("+37"));
  out.clear();
  // Windows auto-repeats modifiers; nothing is posted
  QVERIFY(m.keyDown(kScLCtrl, out));
  QVERIFY(m.keyRepeat(kScLCtrl, 3, out));
  QVERIFY(out.empty());
  QVERIFY(m.keyDown(kScW, out));
  QVERIFY(m.keyUp(kScW, out));
  QVERIFY(m.keyUp(kScLCtrl, out));
  QCOMPARE(describe(out), QString("Kd kd -37"));
  QCOMPARE(m.postedModifiers(), MacModifiers(0));

  // swap off: Ctrl stays Control, Win is Command
  WindowsKeyboardMapper n(false);
  out.clear();
  n.keyDown(kScLCtrl, out);
  n.keyDown(kScLWin, out);
  QCOMPARE(describe(out), QString("+3b +37"));
}

void OSXWindowsKeyboardTests::mapperCtrlBackspace()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  m.keyDown(kScLCtrl, out);
  out.clear();
  m.keyDown(kScBackspace, out);
  // Command released before Option is pressed, then Backspace
  QCOMPARE(describe(out), QString("-37 +3a K33"));
  out.clear();
  m.keyRepeat(kScBackspace, 2, out);
  QCOMPARE(describe(out), QString("R33"));
  QCOMPARE(out.back().count, 2);
  out.clear();
  m.keyUp(kScBackspace, out);
  // original state restored
  QCOMPARE(describe(out), QString("k33 -3a +37"));
  out.clear();
  m.keyUp(kScLCtrl, out);
  QCOMPARE(describe(out), QString("-37"));
}

void OSXWindowsKeyboardTests::mapperCtrlTabSticky()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  QCOMPARE(describe(out), QString("+37 -37 +3b K30 k30"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacControlL));
  out.clear();
  // Shift joins the chord: Control stays held
  m.keyDown(kScLShift, out);
  QCOMPARE(describe(out), QString("+38"));
  out.clear();
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  QCOMPARE(describe(out), QString("K30 k30"));
  out.clear();
  m.keyUp(kScLShift, out);
  m.keyUp(kScLCtrl, out);
  QCOMPARE(describe(out), QString("-38 -3b"));

  // another shortcut ends the sticky chord
  out.clear();
  m.keyDown(kScRCtrl, out);
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  out.clear();
  m.keyDown(kScW, out);
  m.keyUp(kScW, out);
  QCOMPARE(describe(out), QString("-3b +36 Kd kd"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacCommandR));
}

void OSXWindowsKeyboardTests::mapperHomeEndShift()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  m.keyDown(kScLShift, out);
  m.keyDown(kScHome, out);
  QCOMPARE(describe(out), QString("+38 +37 K7b"));
  out.clear();
  // Ctrl pressed while Home is held: repeat moves to Up, Left is released first
  m.keyDown(kScLCtrl, out);
  out.clear();
  m.keyRepeat(kScHome, 1, out);
  QCOMPARE(describe(out), QString("k7b R7e"));
  out.clear();
  // key up hits the key that is down now
  m.keyUp(kScHome, out);
  QCOMPARE(describe(out), QString("k7e"));
}

void OSXWindowsKeyboardTests::mapperRepeatAndUnknownKeys()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  QVERIFY(!m.keyDown(0x3a, out));      // Caps Lock -> KeyID path
  QVERIFY(!m.keyDown(0x120, out));     // media key -> KeyID path
  QVERIFY(!m.keyUp(0x3a, out));
  QVERIFY(!m.keyRepeat(kScA, 1, out)); // never went down
  QVERIFY(!m.keyUp(kScA, out));
  QVERIFY(out.empty());

  m.keyDown(kScA, out);
  m.keyDown(kScA, out); // mis-reported repeat
  m.keyUp(kScA, out);
  QCOMPARE(describe(out), QString("K0t R0t k0t"));
}

void OSXWindowsKeyboardTests::mapperAllKeysUp()
{
  WindowsKeyboardMapper m(true);
  KeyActions out;
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScLAlt, out);
  m.keyDown(kScBackspace, out);
  out.clear();
  m.allKeysUp(out);
  QCOMPARE(describe(out), QString("k33 -3a -37"));
  QCOMPARE(m.postedModifiers(), MacModifiers(0));
  QCOMPARE(m.remoteModifiers(), RemoteModifiers(0));
  out.clear();
  QVERIFY(!m.keyUp(kScLCtrl, out));
}

void OSXWindowsKeyboardTests::decideAltTab()
{
  const auto alt = RemoteModifiers(kRemoteAltL);
  auto d = decideKey(kScTab, kVK_Tab, alt, true, true, false);
  QCOMPARE(d.virtualKey, MacVirtualKey(kVK_Tab));
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL));
  QVERIFY(d.switcher);

  d = decideKey(kScTab, kVK_Tab, RemoteModifiers(kRemoteAltL | kRemoteShiftL), true, true, false);
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL | kMacShiftL));

  // disabled, or other modifiers involved: no switcher
  QVERIFY(!decideKey(kScTab, kVK_Tab, alt, true, false, false).switcher);
  QVERIFY(!decideKey(kScTab, kVK_Tab, RemoteModifiers(kRemoteAltL | kRemoteCtrlL), true, true, false).switcher);
  // other keys only join the switcher while it is open
  QVERIFY(!decideKey(kScQ, kVK_ANSI_Q, alt, true, true, false).switcher);
  d = decideKey(kScQ, kVK_ANSI_Q, alt, true, true, true);
  QVERIFY(d.switcher);
  QCOMPARE(d.modifiers, MacModifiers(kMacCommandL));
}

void OSXWindowsKeyboardTests::mapperAltTabSwitcher()
{
  WindowsKeyboardMapper m(true, true, false);
  KeyActions out;
  m.keyDown(kScLAlt, out);
  QCOMPARE(describe(out), QString("+3a"));
  out.clear();
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  // Command stays held after Tab is released
  QCOMPARE(describe(out), QString("-3a +37 K30 k30"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacCommandL));
  out.clear();
  // repeated Tab, then another switcher key (Q) and an arrow keep Command
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  m.keyDown(kScQ, out);
  m.keyUp(kScQ, out);
  m.keyDown(kScLeft, out);
  m.keyUp(kScLeft, out);
  QCOMPARE(describe(out), QString("K30 k30 Kc kc K7b k7b"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacCommandL));
  out.clear();
  // releasing Alt closes the switcher and returns to the rest set
  m.keyUp(kScLAlt, out);
  QCOMPARE(describe(out), QString("-37"));
  QCOMPARE(m.postedModifiers(), MacModifiers(0));
  out.clear();
  // after that Alt maps to Option again
  m.keyDown(kScLAlt, out);
  m.keyDown(kScQ, out);
  m.keyUp(kScQ, out);
  QCOMPARE(describe(out), QString("+3a Kc kc"));
}

void OSXWindowsKeyboardTests::mapperAltShiftTabBackwards()
{
  WindowsKeyboardMapper m(true, true, false);
  KeyActions out;
  m.keyDown(kScLShift, out);
  m.keyDown(kScLAlt, out);
  out.clear();
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  QCOMPARE(describe(out), QString("-3a +37 K30 k30"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacCommandL | kMacShiftL));
  out.clear();
  // Shift released while Alt is still held: switcher stays open
  m.keyUp(kScLShift, out);
  QCOMPARE(describe(out), QString("-38"));
  QCOMPARE(m.postedModifiers(), MacModifiers(kMacCommandL));
  out.clear();
  m.keyUp(kScLAlt, out);
  QCOMPARE(describe(out), QString("-37"));
}

void OSXWindowsKeyboardTests::mapperAltTabDisabled()
{
  WindowsKeyboardMapper m(true, false, false);
  KeyActions out;
  m.keyDown(kScLAlt, out);
  m.keyDown(kScTab, out);
  m.keyUp(kScTab, out);
  QCOMPARE(describe(out), QString("+3a K30 k30"));
}

void OSXWindowsKeyboardTests::mapperCtrlShiftChord()
{
  WindowsKeyboardMapper m(true, true, true);
  KeyActions out;

  // Ctrl, Shift, release Shift first: one switch, modifiers still posted
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScLShift, out);
  QCOMPARE(describe(out), QString("+37 +38"));
  out.clear();
  m.keyUp(kScLShift, out);
  QCOMPARE(describe(out), QString("S -38"));
  out.clear();
  m.keyUp(kScLCtrl, out);
  QCOMPARE(describe(out), QString("-37"));

  // Shift first, release Ctrl first (right-hand keys)
  out.clear();
  m.keyDown(kScRShift, out);
  m.keyDown(kScRCtrl, out);
  out.clear();
  m.keyUp(kScRCtrl, out);
  QCOMPARE(describe(out), QString("S -36"));
  out.clear();
  m.keyUp(kScRShift, out);
  QCOMPARE(describe(out), QString("-3c"));
}

void OSXWindowsKeyboardTests::mapperCtrlShiftChordCancelled()
{
  WindowsKeyboardMapper m(true, true, true);
  auto switches = [](const KeyActions &out) {
    int n = 0;
    for (const auto &a : out)
      n += a.switchInputSource ? 1 : 0;
    return n;
  };
  KeyActions out;

  // Ctrl+Shift+T
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScLShift, out);
  m.keyDown(kScT, out);
  m.keyUp(kScT, out);
  m.keyUp(kScLShift, out);
  m.keyUp(kScLCtrl, out);
  QCOMPARE(switches(out), 0);

  // Ctrl+T, then Shift, then release
  out.clear();
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScT, out);
  m.keyUp(kScT, out);
  m.keyDown(kScLShift, out);
  m.keyUp(kScLShift, out);
  m.keyUp(kScLCtrl, out);
  QCOMPARE(switches(out), 0);

  // Shift+A typed, then Ctrl pressed and released while Shift is held
  out.clear();
  m.keyDown(kScLShift, out);
  m.keyDown(kScA, out);
  m.keyUp(kScA, out);
  m.keyDown(kScLCtrl, out);
  m.keyUp(kScLCtrl, out);
  m.keyUp(kScLShift, out);
  QCOMPARE(switches(out), 0);

  // Alt joins the chord
  out.clear();
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScLShift, out);
  m.keyDown(kScLAlt, out);
  m.keyUp(kScLAlt, out);
  m.keyUp(kScLShift, out);
  m.keyUp(kScLCtrl, out);
  QCOMPARE(switches(out), 0);

  // a clean chord still works afterwards, and only once
  out.clear();
  m.keyDown(kScLCtrl, out);
  m.keyDown(kScLShift, out);
  m.keyUp(kScLCtrl, out);
  m.keyUp(kScLShift, out);
  QCOMPARE(switches(out), 1);

  // disabled setting: no action
  WindowsKeyboardMapper off(true, true, false);
  out.clear();
  off.keyDown(kScLCtrl, out);
  off.keyDown(kScLShift, out);
  off.keyUp(kScLShift, out);
  off.keyUp(kScLCtrl, out);
  QCOMPARE(switches(out), 0);
}

QTEST_MAIN(OSXWindowsKeyboardTests)

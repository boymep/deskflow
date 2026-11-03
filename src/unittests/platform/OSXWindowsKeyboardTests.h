/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class OSXWindowsKeyboardTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void scancodeTable();
  void scancodeTableUnmapped();
  void typingKeys();
  void pcLayoutCandidates();
  void baseModifiers();
  void decideShortcutsAndText();
  void decideEditingExceptions();
  void mapperPlainShortcut();
  void defaultKeyActionIsNoop();
  void plainAKeyIsKeyKindVk0();
  void mapperCtrlBackspace();
  void mapperCtrlTabSticky();
  void mapperHomeEndShift();
  void mapperRepeatAndUnknownKeys();
  void mapperAllKeysUp();
  void decideAltTab();
  void mapperAltTabSwitcher();
  void mapperAltShiftTabBackwards();
  void mapperAltTabDisabled();
  void mapperCtrlShiftChord();
  void mapperCtrlShiftChordCancelled();
  void altTabModeNames();
  void mapperBuiltinOpenNavigateCommit();
  void mapperBuiltinShiftOpensReverse();
  void mapperBuiltinEscCancels();
  void mapperBuiltinEnterCommits();
  void mapperBuiltinOtherKeyCancels();
  void mapperBuiltinCtrlCancels();
  void mapperBuiltinRepeat();
  void mapperBuiltinAllKeysUp();
  void mapperBuiltinNotWithCtrlOrWin();
};

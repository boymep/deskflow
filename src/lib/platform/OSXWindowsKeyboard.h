/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deskflow::osx {

//! Windows server keyboard mode for the macOS client
/*!
Pure translation logic (no window server access) used by OSXKeyState when the
client option "Windows server keyboard" is enabled. Remote keys are identified
by the Windows set-1 scancode the server sends as the key button (bit 0x100 marks
extended keys) and mapped to the macOS virtual key at the same physical position.

Only events the client synthesizes for the server are affected; the Mac's own
keyboard is never touched.
*/

//! Client settings for the Windows server keyboard mode.
struct WindowsKeyboardOptions
{
  bool enabled = false;    //!< master switch; off = upstream behaviour
  bool swapCtrlCmd = true; //!< remote Ctrl -> Command, Windows key -> Control
  bool altTabAppSwitcher = true;       //!< remote Alt+Tab -> Command+Tab (macOS app switcher)
  bool ctrlShiftSwitchesLayout = true; //!< remote Ctrl+Shift alone -> next Mac input source
};

//! Windows set-1 scancode (0x100 = extended) of a remote key, masked to 9 bits.
using WindowsScancode = uint16_t;

//! macOS virtual key code (kVK_*).
using MacVirtualKey = uint16_t;

//! Maps a Windows scancode to the macOS virtual key at the same physical position.
/*!
Returns no value for keys without a physical equivalent (media keys, Pause,
Print Screen, Caps/Num/Scroll Lock); those use the regular KeyID path.
Modifier scancodes map to their natural (unswapped) macOS modifier key.
*/
std::optional<MacVirtualKey> windowsScancodeToMacVirtualKey(WindowsScancode scancode);

//! True for keys that type a character (letters, digits, punctuation, ISO key).
bool isMacTypingKey(MacVirtualKey virtualKey);

//! Input source IDs to try, in order, as the "PC" variant of a Mac layout.
/*!
For example com.apple.keylayout.Russian -> com.apple.keylayout.RussianWin.
Returns an empty list when \p inputSourceId is already a PC variant.
*/
std::vector<std::string> pcLayoutCandidates(const std::string &inputSourceId);

//! True when \p inputSourceId already is a Windows/PC style layout.
bool isPcLayout(const std::string &inputSourceId);

//! Remote (Windows) modifier keys currently held, one bit per physical key.
enum RemoteModifier : uint8_t
{
  kRemoteShiftL = 1 << 0,
  kRemoteShiftR = 1 << 1,
  kRemoteCtrlL = 1 << 2,
  kRemoteCtrlR = 1 << 3,
  kRemoteAltL = 1 << 4,
  kRemoteAltR = 1 << 5,
  kRemoteWinL = 1 << 6,
  kRemoteWinR = 1 << 7,
};
using RemoteModifiers = uint8_t;

//! macOS modifier keys the client has posted as held, one bit per key.
enum MacModifier : uint8_t
{
  kMacShiftL = 1 << 0,
  kMacShiftR = 1 << 1,
  kMacControlL = 1 << 2,
  kMacControlR = 1 << 3,
  kMacOptionL = 1 << 4,
  kMacOptionR = 1 << 5,
  kMacCommandL = 1 << 6,
  kMacCommandR = 1 << 7,
};
using MacModifiers = uint8_t;

//! Returns the remote modifier bit for a modifier scancode, or 0.
RemoteModifier remoteModifierForScancode(WindowsScancode scancode);

//! macOS virtual key of a single MacModifier bit.
MacVirtualKey macVirtualKeyForModifier(MacModifier modifier);

//! Mac modifiers that represent the held remote modifiers outside of exceptions.
MacModifiers baseMacModifiers(RemoteModifiers remote, bool swapCtrlCmd);

//! How one remote non-modifier key press is delivered to macOS.
struct KeyDecision
{
  MacVirtualKey virtualKey = 0;  //!< key to post (may differ from the physical key, e.g. Home -> Left)
  MacModifiers modifiers = 0;    //!< Mac modifiers that must be held while the key is posted
  bool sticky = false;           //!< keep \c modifiers after key up until the triggering modifier is released
  bool switcher = false;         //!< Alt+Tab app switcher: Command stays while Alt is held, other keys use Command
  bool allowText = false;        //!< plain typing: may carry the Windows character of the current language
  MacVirtualKey textKey = 0;     //!< virtual key to translate for the character (valid if allowText)
};

//! Decides how physical key \p virtualKey (from the scancode table) is posted.
/*!
Applies the Windows editing-key exceptions (Ctrl+Backspace, Home/End, ...) and
the Ctrl+Tab exception; otherwise returns the base modifier mapping.
*/
/*!
With \p altTabSwitcher, Alt+Tab (no Ctrl/Win) becomes Command+Tab. While the switcher is open
(\p switcherOpen, i.e. Alt still held after Alt+Tab) every other key is posted with Command too,
so switcher shortcuts (arrows, Q, W, H, Esc) work as on a Mac.
*/
KeyDecision decideKey(
    WindowsScancode scancode, MacVirtualKey virtualKey, RemoteModifiers remote, bool swapCtrlCmd,
    bool altTabSwitcher = false, bool switcherOpen = false
);

//! One event the platform layer must post, in order.
struct KeyAction
{
  MacVirtualKey virtualKey = 0;
  bool down = false;
  bool isModifier = false; //!< flagsChanged for a modifier key
  int32_t count = 1;       //!< number of auto-repeats (only if \c repeat)
  bool repeat = false;     //!< auto-repeat of a held key
  bool text = false;            //!< attach the Windows character of \c textKey
  MacVirtualKey textKey = 0;    //!< virtual key to translate for the character
  bool shift = false;           //!< remote Shift held (for the character)
  bool switchInputSource = false; //!< not a key event: select the next Mac input source
};
using KeyActions = std::vector<KeyAction>;

//! State machine turning remote key events into macOS key events.
/*!
Modifier consistency: the Mac always sees, via posted modifier key events, the
"rest" modifier set (the base mapping of the held remote modifiers, e.g. Ctrl ->
Command). Before a non-modifier key goes down, the posted set is reconciled to
the set its KeyDecision requires (e.g. Ctrl+Backspace releases Command and
presses Option); after the key goes up, it is reconciled back to the rest set.
Sticky decisions (Ctrl+Tab -> Control+Tab) become the rest set until the
triggering remote modifier is released, so tab pickers stay open while Ctrl is
held. Releases are always posted before presses.

Alt+Tab opens the macOS app switcher: Command is held (instead of Option) and
stays held, together with any other key pressed meanwhile, until Alt is released.

Ctrl+Shift chord: when remote Ctrl and Shift were held together with no other
key or modifier since all modifiers were last released, releasing either one
emits a KeyAction with switchInputSource set (once per chord).
*/
class WindowsKeyboardMapper
{
public:
  explicit WindowsKeyboardMapper(bool swapCtrlCmd, bool altTabSwitcher = false, bool ctrlShiftSwitch = false);

  //! Returns false if the key is not handled here (use the KeyID path).
  bool keyDown(WindowsScancode button, KeyActions &out);
  //! Returns false if the key is not handled here (use the KeyID path).
  bool keyRepeat(WindowsScancode button, int32_t count, KeyActions &out);
  //! Returns false if the key is not handled here (use the KeyID path).
  bool keyUp(WindowsScancode button, KeyActions &out);
  //! Releases everything this mapper has pressed.
  void allKeysUp(KeyActions &out);

  RemoteModifiers remoteModifiers() const
  {
    return m_remote;
  }
  MacModifiers postedModifiers() const
  {
    return m_posted;
  }

private:
  struct PressedKey
  {
    bool down = false;
    MacVirtualKey virtualKey = 0;
    bool text = false;
    MacVirtualKey textKey = 0;
  };

  void modifierChanged(KeyActions &out);
  MacModifiers restModifiers();
  void reconcile(MacModifiers target, KeyActions &out);
  KeyAction keyAction(const KeyDecision &decision, bool down) const;

  static constexpr size_t kNumScancodes = 0x200;

  bool m_swapCtrlCmd;
  bool m_altTabSwitcher;
  bool m_ctrlShiftSwitch;
  bool m_switcherOpen = false;
  bool m_chordClean = true; //!< only Ctrl/Shift pressed since all modifiers were last released
  bool m_chordArmed = false; //!< Ctrl and Shift are both held and the chord is still clean
  RemoteModifiers m_remote = 0;
  MacModifiers m_posted = 0;
  WindowsScancode m_sticky = 0;
  std::array<PressedKey, kNumScancodes> m_pressed{};
};

} // namespace deskflow::osx

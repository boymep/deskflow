/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDialog>

class SettingsDialogButtonBox;

namespace Ui {
class ClientConfigDialog;
}

/**
 * @brief The ClientConfigDialog class
 * Simple dialog to allow users to configure the Client settings
 */
class ClientConfigDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ClientConfigDialog(QWidget *parent = nullptr);
  ~ClientConfigDialog() override;

protected:
  void changeEvent(QEvent *e) override;

private:
  /**
   * @brief updateControls update widget enable state
   */
  void updateControls() const;

  /**
   * @brief initConnections
   * Sets up all the connections
   */
  void initConnections();

  //! Shows the window thumbnail permission state (macOS 14+ only).
  void updateThumbnailControls() const;
  //! Asks for the Screen Recording permission, or opens System Settings if macOS no longer prompts.
  void requestThumbnails();

  //! Fills the Alt+Tab mode combobox (translated labels, setting values as item data).
  void fillAltTabModes() const;
  //! Setting value of the selected Alt+Tab mode.
  QString altTabMode() const;
  //! Selects \p mode, or the first entry if unknown.
  void setAltTabMode(const QString &mode) const;

  /**
   * @brief isModified
   * @return true when any client settings in the gui do not match the stored settings values.
   */
  bool isModified() const;

  /**
   * @brief isDefault
   * @return true if all client settings match the default values
   */
  bool isDefault() const;

  /**
   * @brief setButtonBoxEnabledButtons
   * Enable / Disable the button box buttons based on the state of the gui
   */
  void setButtonBoxEnabledButtons() const;

  /**
   * @brief Load the client setting into the gui
   */
  void load();

  /**
   * @brief Set the gui values to the defalut values for all client settings
   */
  void resetToDefault();

  /**
   * @brief save to settings and then calls QDialog::accept
   */
  void save();

  Ui::ClientConfigDialog *ui = nullptr;
  SettingsDialogButtonBox *m_buttonBox = nullptr;
  bool m_thumbnailsRequested = false; //!< the permission was asked for once while the dialog is open
};

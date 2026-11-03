/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientConfigDialog.h"
#include "ui_ClientConfigDialog.h"

#include "common/Settings.h"
#include "gui/widgets/SettingsDialogButtonBox.h"

#include <QOperatingSystemVersion>
#include <QPushButton>
#include <QSignalBlocker>

#if defined(Q_OS_MACOS)
#include "gui/OSXHelpers.h"
#endif

namespace {

//! Window switcher thumbnails use ScreenCaptureKit screenshots, available from macOS 14.
bool thumbnailsSupported()
{
#if defined(Q_OS_MACOS)
  return QOperatingSystemVersion::current() >= QOperatingSystemVersion::MacOSSonoma;
#else
  return false;
#endif
}

} // namespace

ClientConfigDialog::ClientConfigDialog(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::ClientConfigDialog),
      m_buttonBox{new SettingsDialogButtonBox(this)}
{
  ui->setupUi(this);
  ui->widgetThumbnails->setVisible(thumbnailsSupported());
  fillAltTabModes();
  layout()->addWidget(m_buttonBox);
  initConnections();
  load();
  updateControls();
  setButtonBoxEnabledButtons();
}

ClientConfigDialog::~ClientConfigDialog()
{
  delete ui;
}

void ClientConfigDialog::changeEvent(QEvent *e)
{
  QDialog::changeEvent(e);
  if (e->type() == QEvent::LanguageChange) {
    const auto mode = altTabMode();
    ui->retranslateUi(this);
    fillAltTabModes();
    setAltTabMode(mode);
    updateThumbnailControls();
  }
}

void ClientConfigDialog::fillAltTabModes() const
{
  const QSignalBlocker blocker(ui->cbAltTabMode);
  ui->cbAltTabMode->clear();
  ui->cbAltTabMode->addItem(tr("Window switcher (built-in)"), QStringLiteral("builtin"));
  ui->cbAltTabMode->addItem(tr("macOS app switcher (Command+Tab)"), QStringLiteral("system"));
  ui->cbAltTabMode->addItem(tr("Option+Tab (for third-party switchers)"), QStringLiteral("option"));
}

QString ClientConfigDialog::altTabMode() const
{
  return ui->cbAltTabMode->currentData().toString();
}

void ClientConfigDialog::setAltTabMode(const QString &mode) const
{
  const int index = ui->cbAltTabMode->findData(mode);
  ui->cbAltTabMode->setCurrentIndex(index >= 0 ? index : 0);
}

void ClientConfigDialog::updateControls() const
{
  const auto writable = Settings::isWritable();
  ui->cbDynamicConnectTime->setEnabled(writable);
  // language sync only affects character mapping, which the Windows keyboard mode bypasses
  ui->cbLanguageSync->setEnabled(writable && !ui->cbPhysicalKeys->isChecked());
  ui->cbPhysicalKeys->setEnabled(writable);
  ui->cbSwapCtrlCmd->setEnabled(writable && ui->cbPhysicalKeys->isChecked());
  ui->lblAltTabMode->setEnabled(writable && ui->cbPhysicalKeys->isChecked());
  ui->cbAltTabMode->setEnabled(writable && ui->cbPhysicalKeys->isChecked());
  ui->cbCtrlShift->setEnabled(writable && ui->cbPhysicalKeys->isChecked());
  updateThumbnailControls();
  ui->cbSideButtons->setEnabled(writable);
  ui->cbXScrollInvert->setEnabled(writable);
  ui->sbXScrollScale->setEnabled(writable);
  ui->cbYScrollInvert->setEnabled(writable);
  ui->sbYScrollScale->setEnabled(writable);
}

void ClientConfigDialog::updateThumbnailControls() const
{
  if (!thumbnailsSupported()) {
    return;
  }
#if defined(Q_OS_MACOS)
  const bool builtin = ui->cbPhysicalKeys->isChecked() && altTabMode() == QStringLiteral("builtin");
  const bool granted = hasOSXScreenCapturePermission();
  ui->btnThumbnails->setVisible(!granted);
  ui->btnThumbnails->setEnabled(builtin);
  ui->lblThumbnails->setEnabled(builtin);
  if (granted) {
    ui->lblThumbnails->setText(tr("Window thumbnails are allowed."));
  } else if (m_thumbnailsRequested) {
    ui->btnThumbnails->setText(tr("Open System Settings…"));
    ui->lblThumbnails->setText(
        tr("Turn on Deskflow in Privacy & Security > Screen & System Audio Recording, then restart Deskflow.")
    );
  } else {
    ui->btnThumbnails->setText(tr("Allow window thumbnails…"));
    ui->lblThumbnails->setText(tr("The window switcher shows app icons only."));
  }
#endif
}

void ClientConfigDialog::requestThumbnails()
{
#if defined(Q_OS_MACOS)
  // the permission belongs to the Deskflow app bundle, which also covers deskflow-core
  // (started by this process); macOS prompts only the first time, later go to System Settings
  if (m_thumbnailsRequested) {
    openOSXScreenCaptureSettings();
  } else {
    requestOSXScreenCapturePermission();
    m_thumbnailsRequested = true;
  }
  updateThumbnailControls();
#endif
}

void ClientConfigDialog::initConnections()
{
  connect(m_buttonBox, &SettingsDialogButtonBox::accepted, this, &ClientConfigDialog::save);
  connect(m_buttonBox, &SettingsDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_buttonBox, &SettingsDialogButtonBox::reset, this, &ClientConfigDialog::load);
  connect(m_buttonBox, &SettingsDialogButtonBox::restoreDefault, this, &ClientConfigDialog::resetToDefault);

  connect(
      ui->cbDynamicConnectTime, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons
  );
  connect(ui->cbLanguageSync, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbPhysicalKeys, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbPhysicalKeys, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::updateControls);
  connect(ui->cbSwapCtrlCmd, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbAltTabMode, &QComboBox::currentIndexChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbAltTabMode, &QComboBox::currentIndexChanged, this, &ClientConfigDialog::updateThumbnailControls);
  connect(ui->btnThumbnails, &QPushButton::clicked, this, &ClientConfigDialog::requestThumbnails);
  connect(ui->cbCtrlShift, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbSideButtons, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbYScrollInvert, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->sbYScrollScale, &QDoubleSpinBox::valueChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->cbXScrollInvert, &QCheckBox::checkStateChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(ui->sbXScrollScale, &QDoubleSpinBox::valueChanged, this, &ClientConfigDialog::setButtonBoxEnabledButtons);
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, &ClientConfigDialog::updateControls);
}

bool ClientConfigDialog::isModified() const
{
  return (ui->cbDynamicConnectTime->isChecked() != Settings::value(Settings::Client::DynamicConnectionRetry).toBool()
         ) ||
         (ui->cbLanguageSync->isChecked() != Settings::value(Settings::Client::LanguageSync).toBool()) ||
         (ui->cbPhysicalKeys->isChecked() != Settings::value(Settings::Client::PhysicalKeys).toBool()) ||
         (ui->cbSwapCtrlCmd->isChecked() != Settings::value(Settings::Client::SwapCtrlCmd).toBool()) ||
         (altTabMode() != Settings::value(Settings::Client::AltTabMode).toString()) ||
         (ui->cbCtrlShift->isChecked() != Settings::value(Settings::Client::CtrlShiftSwitchesLayout).toBool()) ||
         (ui->cbSideButtons->isChecked() != Settings::value(Settings::Client::SideButtonsSwitchSpaces).toBool()) ||
         (ui->cbYScrollInvert->isChecked() != Settings::value(Settings::Client::InvertYScroll).toBool()) ||
         (ui->sbYScrollScale->value() != Settings::value(Settings::Client::YScrollScale).toDouble()) ||
         (ui->cbXScrollInvert->isChecked() != Settings::value(Settings::Client::InvertXScroll).toBool()) ||
         (ui->sbXScrollScale->value() != Settings::value(Settings::Client::XScrollScale).toDouble());
}

bool ClientConfigDialog::isDefault() const
{
  return (ui->cbDynamicConnectTime->isChecked() ==
          Settings::defaultValue(Settings::Client::DynamicConnectionRetry).toBool()) &&
         (ui->cbLanguageSync->isChecked() == Settings::defaultValue(Settings::Client::LanguageSync).toBool()) &&
         (ui->cbPhysicalKeys->isChecked() == Settings::defaultValue(Settings::Client::PhysicalKeys).toBool()) &&
         (ui->cbSwapCtrlCmd->isChecked() == Settings::defaultValue(Settings::Client::SwapCtrlCmd).toBool()) &&
         (altTabMode() == Settings::defaultValue(Settings::Client::AltTabMode).toString()) &&
         (ui->cbCtrlShift->isChecked() == Settings::defaultValue(Settings::Client::CtrlShiftSwitchesLayout).toBool()) &&
         (ui->cbSideButtons->isChecked() == Settings::defaultValue(Settings::Client::SideButtonsSwitchSpaces).toBool()) &&
         (ui->cbYScrollInvert->isChecked() == Settings::defaultValue(Settings::Client::InvertYScroll).toBool()) &&
         (ui->sbYScrollScale->value() == Settings::defaultValue(Settings::Client::YScrollScale).toDouble()) &&
         (ui->cbXScrollInvert->isChecked() == Settings::defaultValue(Settings::Client::InvertXScroll).toBool()) &&
         (ui->sbXScrollScale->value() == Settings::defaultValue(Settings::Client::XScrollScale).toDouble());
}

void ClientConfigDialog::setButtonBoxEnabledButtons() const
{
  const bool modified = isModified();
  m_buttonBox->enableSave(modified);
  m_buttonBox->enableReset(modified);
  m_buttonBox->enableRestoreDefaults(!isDefault());
}

void ClientConfigDialog::load()
{
  ui->cbDynamicConnectTime->setChecked(Settings::value(Settings::Client::DynamicConnectionRetry).toBool());
  ui->cbLanguageSync->setChecked(Settings::value(Settings::Client::LanguageSync).toBool());
  ui->cbPhysicalKeys->setChecked(Settings::value(Settings::Client::PhysicalKeys).toBool());
  ui->cbSwapCtrlCmd->setChecked(Settings::value(Settings::Client::SwapCtrlCmd).toBool());
  setAltTabMode(Settings::value(Settings::Client::AltTabMode).toString());
  ui->cbCtrlShift->setChecked(Settings::value(Settings::Client::CtrlShiftSwitchesLayout).toBool());
  ui->cbSideButtons->setChecked(Settings::value(Settings::Client::SideButtonsSwitchSpaces).toBool());
  ui->cbYScrollInvert->setChecked(Settings::value(Settings::Client::InvertYScroll).toBool());
  ui->sbYScrollScale->setValue(Settings::value(Settings::Client::YScrollScale).toDouble());
  ui->cbXScrollInvert->setChecked(Settings::value(Settings::Client::InvertXScroll).toBool());
  ui->sbXScrollScale->setValue(Settings::value(Settings::Client::XScrollScale).toDouble());
}

void ClientConfigDialog::resetToDefault()
{
  ui->cbDynamicConnectTime->setChecked(Settings::defaultValue(Settings::Client::DynamicConnectionRetry).toBool());
  ui->cbLanguageSync->setChecked(Settings::defaultValue(Settings::Client::LanguageSync).toBool());
  ui->cbPhysicalKeys->setChecked(Settings::defaultValue(Settings::Client::PhysicalKeys).toBool());
  ui->cbSwapCtrlCmd->setChecked(Settings::defaultValue(Settings::Client::SwapCtrlCmd).toBool());
  setAltTabMode(Settings::defaultValue(Settings::Client::AltTabMode).toString());
  ui->cbCtrlShift->setChecked(Settings::defaultValue(Settings::Client::CtrlShiftSwitchesLayout).toBool());
  ui->cbSideButtons->setChecked(Settings::defaultValue(Settings::Client::SideButtonsSwitchSpaces).toBool());
  ui->cbYScrollInvert->setChecked(Settings::defaultValue(Settings::Client::InvertYScroll).toBool());
  ui->sbYScrollScale->setValue(Settings::defaultValue(Settings::Client::YScrollScale).toDouble());
  ui->cbXScrollInvert->setChecked(Settings::defaultValue(Settings::Client::InvertXScroll).toBool());
  ui->sbXScrollScale->setValue(Settings::defaultValue(Settings::Client::XScrollScale).toDouble());
}

void ClientConfigDialog::save()
{
  Settings::setValue(Settings::Client::DynamicConnectionRetry, ui->cbDynamicConnectTime->isChecked());
  Settings::setValue(Settings::Client::LanguageSync, ui->cbLanguageSync->isChecked());
  Settings::setValue(Settings::Client::PhysicalKeys, ui->cbPhysicalKeys->isChecked());
  Settings::setValue(Settings::Client::SwapCtrlCmd, ui->cbSwapCtrlCmd->isChecked());
  Settings::setValue(Settings::Client::AltTabMode, altTabMode());
  Settings::setValue(Settings::Client::CtrlShiftSwitchesLayout, ui->cbCtrlShift->isChecked());
  Settings::setValue(Settings::Client::SideButtonsSwitchSpaces, ui->cbSideButtons->isChecked());
  Settings::setValue(Settings::Client::InvertYScroll, ui->cbYScrollInvert->isChecked());
  Settings::setValue(Settings::Client::YScrollScale, ui->sbYScrollScale->value());
  Settings::setValue(Settings::Client::InvertXScroll, ui->cbXScrollInvert->isChecked());
  Settings::setValue(Settings::Client::XScrollScale, ui->sbXScrollScale->value());
  QDialog::accept();
}

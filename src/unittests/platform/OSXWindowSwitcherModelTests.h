/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class OSXWindowSwitcherModelTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void mruTouchOrder();
  void mruBounded();
  void mruRemoveAndRetain();
  void remoteToken();
  void makeKeyWindowEvent();
  void initialSelection();
  void stepWraps();
  void thumbnailSizeFits();
  void thumbnailSizeInvalid();
  void matchById();
  void matchByTitleAndFrame();
  void matchAmbiguous();
  void captureOrder();
  void spaceStrip();
  void spaceStripSingleDesktop();
  void spaceStripUnknownCurrent();
  void fullscreenTitles();
  void initialSelectionMostRecent();
  void initialSelectionFallbacks();
  void desktopTitle();
  void lruBoundedAndRecent();
  void spaceStepPlanner();
  void spaceStepPlannerAborts();
  void directMethodOrder();
  void directSwitchPlanner();
  void directSwitchPlannerWaitsForTransition();
  void directSwitchPlannerRetargets();
  void spaceStepPosition();
  void spaceStepGate();
  void spacesSettled();
  void desktopHotkeys();
  void hotkeySwitchPlanner();
};

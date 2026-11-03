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
  void orderOnScreenFirst();
  void orderOffScreenByMru();
  void orderFallbackAppAndIndex();
  void orderMinimizedIgnoresStaleZ();
  void orderOtherSpacesMixedByMru();
  void orderOtherSpacesWithoutOnScreen();
  void spaceWindowPlausible();
  void selectOtherSpaces();
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
  void lruBoundedAndRecent();
};

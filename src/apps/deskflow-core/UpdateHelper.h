/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

/**
 * @brief Hidden mode `deskflow-core --install-update <new.app> <target.app> <gui-pid>` (macOS).
 *
 * Runs from the new, already verified bundle. Waits up to 30 s for the GUI and every process of the old
 * bundle to exit, swaps the bundles (rolling back on failure), removes the quarantine flag and opens the
 * app again. Every step goes to ~/Library/Logs/Deskflow/updater.log. On failure the old app is opened
 * again and a result file tells the GUI why. With DESKFLOW_UPDATE_NO_OPEN set no app is opened (dry runs).
 *
 * @return the process exit code: 0 installed, 1 not installed, 2 bad call, 3 timeout.
 */
int runInstallUpdateHelper(int argc, char **argv);

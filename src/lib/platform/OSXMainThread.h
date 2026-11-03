/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <chrono>
#include <functional>

namespace deskflow::osx {

//! How long a worker thread waits for the main queue before giving up.
inline constexpr std::chrono::milliseconds kMainThreadWaitTimeout{1000};

//! True on the process main thread (the one that services the GCD main queue).
bool isMainThread();

/**
 * @brief Guard for HIToolbox text input source (TIS/TSM) wrappers.
 *
 * TIS asserts it runs on the main queue (dispatch_assert_queue) and kills the
 * process otherwise. Returns true on the main thread. Off the main thread it
 * logs an error and returns false so the caller can skip the call; debug builds
 * also assert, to catch new call sites early.
 */
bool requireMainThread(const char *caller);

/**
 * @brief Runs \p fn on the main queue and waits for it at most \p timeout.
 *
 * On the main thread \p fn runs inline. Otherwise it is queued with
 * dispatch_async and the caller waits on a semaphore with a deadline, never
 * with dispatch_sync: the main thread may itself be waiting for the caller
 * (deskflow-core joins its core thread after the Qt event loop ends), and a
 * bounded wait turns that into a delay instead of a deadlock.
 *
 * \p fn is copied and still runs when the wait times out, so it must own
 * everything it touches (capture by value or shared_ptr, never stack references).
 *
 * @return true if \p fn finished before the deadline.
 */
bool runOnMainThreadAndWait(std::function<void()> fn, std::chrono::milliseconds timeout = kMainThreadWaitTimeout);

//! Queues \p fn on the main queue without waiting (inline on the main thread).
void runOnMainThreadAsync(std::function<void()> fn);

} // namespace deskflow::osx

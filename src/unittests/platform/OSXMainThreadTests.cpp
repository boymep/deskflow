/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXMainThreadTests.h"

#include "platform/OSXInputSourceCache.h"
#include "platform/OSXMainThread.h"

#include <CoreFoundation/CoreFoundation.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace deskflow::osx;
using namespace std::chrono_literals;

namespace {

//! Services the main run loop (and so the GCD main queue) until \p done or the deadline.
bool spinMainUntil(const std::atomic<bool> &done, std::chrono::milliseconds limit = 5000ms)
{
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (!done.load() && std::chrono::steady_clock::now() < deadline) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
  }
  return done.load();
}

} // namespace

void OSXMainThreadTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void OSXMainThreadTests::runsInlineOnMainThread()
{
  QVERIFY(isMainThread());
  bool ran = false;
  QVERIFY(runOnMainThreadAndWait([&ran] { ran = true; }, 0ms));
  QVERIFY(ran);
}

void OSXMainThreadTests::workerWaitsForMainQueue()
{
  auto ranOnMain = std::make_shared<std::atomic<bool>>(false);
  std::atomic<bool> finished{false};
  std::atomic<bool> result{false};
  std::atomic<bool> workerOffMain{false};
  std::thread worker([&] {
    workerOffMain = !isMainThread();
    result = runOnMainThreadAndWait([ranOnMain] { ranOnMain->store(isMainThread()); }, 5000ms);
    finished = true;
  });
  QVERIFY(spinMainUntil(finished));
  worker.join();
  QVERIFY(workerOffMain.load());
  QVERIFY(result.load());
  QVERIFY(ranOnMain->load());
}

void OSXMainThreadTests::workerTimesOutWhenMainIsBusy()
{
  // the main thread blocks in join() (like deskflow-core joining its core
  // thread): the worker must give up instead of deadlocking
  auto ran = std::make_shared<std::atomic<bool>>(false);
  bool result = true;
  std::thread worker([&] { result = runOnMainThreadAndWait([ran] { ran->store(true); }, 50ms); });
  worker.join();
  QVERIFY(!result);
  QVERIFY(!ran->load());

  // the queued call owns its data and still runs once the main queue is serviced
  QVERIFY(spinMainUntil(*ran));
}

void OSXMainThreadTests::asyncRunsOnMainQueue()
{
  auto ranOnMain = std::make_shared<std::atomic<bool>>(false);
  std::thread worker([ranOnMain] { runOnMainThreadAsync([ranOnMain] { ranOnMain->store(isMainThread()); }); });
  worker.join();
  QVERIFY(spinMainUntil(*ranOnMain));
}

void OSXMainThreadTests::requireMainThreadOffMain()
{
  QVERIFY(requireMainThread("test"));
#ifdef NDEBUG
  bool offMain = true;
  std::thread worker([&offMain] { offMain = requireMainThread("test"); });
  worker.join();
  QVERIFY(!offMain);
#endif
}

void OSXMainThreadTests::inputSourceCacheFromWorkerThread()
{
  // regression: reading input sources from the core's network thread called
  // TIS off the main queue and crashed in dispatch_assert_queue
  std::atomic<bool> finished{false};
  std::shared_ptr<const InputSourceSnapshot> full;
  std::shared_ptr<const InputSourceSnapshot> current;
  std::thread worker([&] {
    full = InputSourceCache::instance().refreshAndWait();
    current = InputSourceCache::instance().snapshot();
    finished = true;
  });
  QVERIFY(spinMainUntil(finished));
  worker.join();
  QVERIFY(full != nullptr);
  QVERIFY(current != nullptr);
  QVERIFY(full->groups != nullptr);
  QCOMPARE(current->currentLayoutId, full->currentLayoutId);
}

QTEST_MAIN(OSXMainThreadTests)

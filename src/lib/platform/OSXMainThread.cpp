/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXMainThread.h"

#include "base/Log.h"

#include <cassert>
#include <dispatch/dispatch.h>
#include <memory>
#include <pthread.h>

namespace deskflow::osx {

namespace {

struct MainThreadCall
{
  explicit MainThreadCall(std::function<void()> f) : fn(std::move(f)), done(dispatch_semaphore_create(0))
  {
  }
  ~MainThreadCall()
  {
    dispatch_release(done);
  }
  MainThreadCall(const MainThreadCall &) = delete;
  MainThreadCall &operator=(const MainThreadCall &) = delete;

  std::function<void()> fn;
  dispatch_semaphore_t done;
};

void runMainThreadCall(void *context)
{
  // owns one reference; the waiting thread may already have given up on its own
  std::unique_ptr<std::shared_ptr<MainThreadCall>> call(static_cast<std::shared_ptr<MainThreadCall> *>(context));
  (*call)->fn();
  dispatch_semaphore_signal((*call)->done);
}

void runAsyncCall(void *context)
{
  std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(context));
  (*fn)();
}

} // namespace

bool isMainThread()
{
  return pthread_main_np() != 0;
}

bool requireMainThread(const char *caller)
{
  if (isMainThread()) {
    return true;
  }
  LOG_ERR("%s: text input source api called off the main thread, call skipped", caller);
  assert(false && "text input source api called off the main thread");
  return false;
}

bool runOnMainThreadAndWait(std::function<void()> fn, std::chrono::milliseconds timeout)
{
  if (isMainThread()) {
    fn();
    return true;
  }

  auto call = std::make_shared<MainThreadCall>(std::move(fn));
  dispatch_async_f(dispatch_get_main_queue(), new std::shared_ptr<MainThreadCall>(call), &runMainThreadCall);

  const auto deadline =
      dispatch_time(DISPATCH_TIME_NOW, std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count());
  if (dispatch_semaphore_wait(call->done, deadline) != 0) {
    LOG_WARN("main thread did not respond within %lld ms", static_cast<long long>(timeout.count()));
    return false;
  }
  return true;
}

void runOnMainThreadAsync(std::function<void()> fn)
{
  if (isMainThread()) {
    fn();
    return;
  }
  dispatch_async_f(dispatch_get_main_queue(), new std::function<void()>(std::move(fn)), &runAsyncCall);
}

} // namespace deskflow::osx

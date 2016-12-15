/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>

#include "mcrouter/CarbonRouterInstanceBase.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/ThreadUtil.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/MessageQueue.h"

namespace facebook { namespace memcache { namespace mcrouter {

template <class RouterInfo>
ProxyThread<RouterInfo>::ProxyThread(
    CarbonRouterInstanceBase& router,
    size_t id)
    : evb_(/* enableTimeMeasurement */ false),
      proxy_(Proxy<RouterInfo>::createProxy(router, evb_, id)) {}

template <class RouterInfo>
void ProxyThread<RouterInfo>::spawn() {
  CHECK(state_.exchange(State::RUNNING) == State::STOPPED);
  thread_ = std::thread([this] () { proxyThreadRun(); });
}

template <class RouterInfo>
void ProxyThread<RouterInfo>::stopAndJoin() noexcept {
  if (thread_.joinable() && proxy_->router().pid() == getpid()) {
    CHECK(state_.exchange(State::STOPPING) == State::RUNNING);
    proxy_->sendMessage(ProxyMessage::Type::SHUTDOWN, nullptr);
    CHECK(state_.exchange(State::STOPPED) == State::STOPPING);
    evb_.terminateLoopSoon();
    thread_.join();
  }
}

template <class RouterInfo>
void ProxyThread<RouterInfo>::proxyThreadRun() {
  mcrouterSetThisThreadName(proxy_->router().opts(), "mcrpxy");

  while (state_ == State::RUNNING || proxy_->fiberManager().hasTasks()) {
    evb_.loopOnce();
    proxy_->drainMessageQueue();
  }

  while (state_ != State::STOPPED) {
    evb_.loopOnce();
  }

  /* make sure proxy is deleted on the proxy thread */
  proxy_.reset();
}

}}}  // facebook::memcache::mcrouter
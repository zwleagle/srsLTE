/*
 * Copyright 2013-2019 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

/******************************************************************************
 *  File:         timers.h
 *  Description:  Manually incremented timers. Call a callback function upon
 *                expiry.
 *  Reference:
 *****************************************************************************/

#ifndef SRSLTE_TIMERS_H
#define SRSLTE_TIMERS_H

#include <functional>
#include <queue>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <vector>

#include "srslte/srslte.h"

namespace srslte {
  
class timer_callback 
{
  public: 
    virtual void timer_expired(uint32_t timer_id) = 0; 
}; 
  
class timers
{
public:
  class timer
  {
  public:
    timer(uint32_t id_=0) {id = id_; counter = 0; timeout = 0; running = false; callback = NULL; }
    void set(timer_callback *callback_, uint32_t timeout_) {
      callback = callback_; 
      timeout = timeout_; 
      reset();
    }
    bool is_running() {
      return (counter < timeout) && running; 
    }
    bool is_expired() {
      return (timeout > 0) && (counter >= timeout);
    }
    uint32_t get_timeout() {
      return timeout; 
    }
    void reset() {
      counter = 0; 
    }
    uint32_t value() {
      return counter;
    }
    void step() {
      if (running) {
        counter++;
        if (is_expired()) {
          running = false; 
          if (callback) {
            callback->timer_expired(id); 
          }
        }        
      }
    }
    void stop() {
      running = false; 
    }
    void run() {
      running = true; 
    }
    uint32_t id; 
  private: 
    timer_callback *callback; 
    uint32_t timeout; 
    uint32_t counter; 
    bool running;
  };

  timers(uint32_t nof_timers_) : timer_list(nof_timers_), used_timers(nof_timers_)
  {
    nof_timers      = nof_timers_;
    next_timer      = 0;
    nof_used_timers = 0;
    for (uint32_t i = 0; i < nof_timers; i++) {
      timer_list[i].id = i;
      used_timers[i] = false;
    }
  }
  
  void step_all() {
    for (uint32_t i=0;i<nof_timers;i++) {
      get(i)->step();
    }
  }
  void stop_all() {
    for (uint32_t i=0;i<nof_timers;i++) {
      get(i)->stop();
    }
  }
  void run_all() {
    for (uint32_t i=0;i<nof_timers;i++) {
      get(i)->run();
    }
  }
  void reset_all() {
    for (uint32_t i=0;i<nof_timers;i++) {
      get(i)->reset();
    }
  }
  timer *get(uint32_t i) {
    if (i < nof_timers) {
      return &timer_list[i];       
    } else {
      printf("Error accessing invalid timer %d (Only %d timers available)\n", i, nof_timers);
      return NULL; 
    }
  }
  void release_id(uint32_t i) {
    if (nof_used_timers > 0 && i < nof_timers) {
      used_timers[i] = false;
      nof_used_timers--;
    } else {
      ERROR("Error releasing timer id=%d: nof_used_timers=%d, nof_timers=%d\n", i, nof_used_timers, nof_timers);
    }
  }
  uint32_t get_unique_id() {
    if (nof_used_timers >= nof_timers) {
      ERROR("Error getting unique timer id: no more timers available\n");
      return 0;
    } else {
      for (uint32_t i=0;i<nof_timers;i++) {
        if (!used_timers[i]) {
          used_timers[i] = true;
          nof_used_timers++;
          return i;
        }
      }
      ERROR("Error getting unique timer id: no more timers available but nof_used_timers=%d, nof_timers=%d\n",
            nof_used_timers,
            nof_timers);
      return 0;
    }
  }
private:
  uint32_t next_timer;
  uint32_t nof_used_timers;
  uint32_t nof_timers;
  std::vector<timer>   timer_list;
  std::vector<bool>    used_timers;
};

class timers2
{
  struct timer_impl {
    timers2*                      parent;
    uint32_t                      duration = 0, timeout = 0;
    bool                          running = false;
    bool                          active  = false;
    std::function<void(uint32_t)> callback;

    explicit timer_impl(timers2* parent_) : parent(parent_) {}
    uint32_t id() const { return std::distance((const timers2::timer_impl*)&parent->timer_list[0], this); }
    bool     is_running() const { return active and running and timeout > 0; }
    bool     is_expired() const { return active and not running and timeout > 0; }

    void set(uint32_t duration_, std::function<void(int)> callback_)
    {
      if (not active) {
        ERROR("Error: setting inactive timer id=%d\n", id());
        return;
      }
      callback = std::move(callback_);
      duration = duration_;
      running  = false; // invalidates any on-going run
    }

    void set(uint32_t duration_)
    {
      if (not active) {
        ERROR("Error: setting inactive timer id=%d\n", id());
        return;
      }
      duration = duration_;
      running  = false; // invalidates any on-going run
    }

    void run()
    {
      if (not active) {
        ERROR("Error: calling run() for inactive timer id=%d\n", id());
        return;
      }
      timeout = parent->cur_time + duration;
      parent->running_timers.emplace(parent, id(), timeout);
      running = true;
    }

    void clear()
    {
      timeout  = 0;
      duration = 0;
      running  = false;
      active   = false;
      callback = std::function<void(uint32_t)>();
      // leave run_id unchanged;
    }

    void trigger()
    {
      if (is_running()) {
        callback(id());
        running = false;
      }
    }
  };

public:
  class unique_timer
  {
  public:
    explicit unique_timer(timers2* parent_, uint32_t timer_id_) : parent(parent_), timer_id(timer_id_) {}
    unique_timer(const unique_timer&) = delete;
    unique_timer(unique_timer&& other) noexcept : parent(other.parent), timer_id(other.timer_id)
    {
      other.parent = nullptr;
    }
    ~unique_timer()
    {
      if (parent) {
        // does not call callback
        impl()->clear();
      }
    }
    unique_timer& operator=(const unique_timer&) = delete;
    unique_timer& operator                       =(unique_timer&& other) noexcept
    {
      if (this != &other) {
        timer_id     = other.timer_id;
        parent       = other.parent;
        other.parent = nullptr;
      }
      return *this;
    }

    void     set(uint32_t duration_, const std::function<void(int)>& callback_) { impl()->set(duration_, callback_); }
    void     set(uint32_t duration_) { impl()->set(duration_); }
    bool     is_running() const { return impl()->is_running(); }
    bool     is_expired() const { return impl()->is_expired(); }
    void     run() { impl()->run(); }
    void     stop() { impl()->running = false; }
    uint32_t id() const { return timer_id; }

  private:
    timer_impl*       impl() { return &parent->timer_list[timer_id]; }
    const timer_impl* impl() const { return &parent->timer_list[timer_id]; }

    timers2* parent;
    uint32_t timer_id;
  };

  void step_all()
  {
    cur_time++;
    while (not running_timers.empty() and cur_time > running_timers.top().timeout) {
      timer_impl* ptr = &timer_list[running_timers.top().timer_id];
      // if the timer_run and timer_impl timeouts do not match, it means that timer_impl::timeout was overwritten.
      // in such case, do not trigger
      if (ptr->timeout == running_timers.top().timeout) {
        ptr->trigger();
      }
      running_timers.pop();
    }
  }

  void stop_all()
  {
    // does not call callback
    while (not running_timers.empty()) {
      running_timers.pop();
    }
    for (uint32_t i = 0; i < timer_list.size(); ++i) {
      timer_list[i].running = false;
    }
  }

  unique_timer get_unique_timer()
  {
    uint32_t i = 0;
    for (; i < timer_list.size(); ++i) {
      if (not timer_list[i].active) {
        break;
      }
    }
    if (i == timer_list.size()) {
      timer_list.emplace_back(this);
    }
    timer_list[i].active = true;
    return unique_timer(this, i);
  }

  uint32_t get_cur_time() const { return cur_time; }

private:
  struct timer_run {
    timers2* parent;
    uint32_t timer_id;
    uint32_t timeout;
    timer_run(timers2* parent_, uint32_t timer_id_, uint32_t timeout_) :
      parent(parent_),
      timer_id(timer_id_),
      timeout(timeout_)
    {
    }
    bool operator<(const timer_run& other) const { return timeout > other.timeout; }
  };

  std::vector<timer_impl>        timer_list;
  std::priority_queue<timer_run> running_timers;
  uint32_t                       cur_time = 0;
};

} // namespace srslte

#endif // SRSLTE_TIMERS_H

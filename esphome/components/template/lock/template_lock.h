#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/lock/lock.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateLockBase : public lock::Lock, public Component {
 public:
  TemplateLockBase()
      : lock_trigger_(new Trigger<>()), unlock_trigger_(new Trigger<>()), open_trigger_(new Trigger<>()) {}

  void loop() override {
    if (!this->f_.has_value())
      return;
    auto val = (*this->f_)();
    if (!val.has_value())
      return;

    this->publish_state(*val);
  }

  void dump_config() override;

  Trigger<> *get_lock_trigger() const { return this->lock_trigger_; }
  Trigger<> *get_unlock_trigger() const { return this->unlock_trigger_; }
  Trigger<> *get_open_trigger() const { return this->open_trigger_; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void control(const lock::LockCall &call) override;
  void open_latch() override;

  optional<F> f_;
  bool optimistic_{false};
  Trigger<> *lock_trigger_;
  Trigger<> *unlock_trigger_;
  Trigger<> *open_trigger_;
  Trigger<> *prev_trigger_{nullptr};
};

class TemplateLock : public TemplateLockBase<std::function<optional<lock::LockState>()>> {
 public:
  void set_state_lambda(std::function<optional<lock::LockState>()> &&f) { this->f_ = f; }
};

/** Optimized template lock for stateless lambdas (no capture).
 *
 * Uses function pointers instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function) per lambda.
 */
class StatelessTemplateLock : public TemplateLockBase<optional<lock::LockState> (*)()> {
 public:
  explicit StatelessTemplateLock(optional<lock::LockState> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateSwitchBase : public switch_::Switch, public Component {
 public:
  TemplateSwitchBase() : turn_on_trigger_(new Trigger<>()), turn_off_trigger_(new Trigger<>()) {}

  void setup() override;
  void dump_config() override;

  void loop() override {
    if (!this->f_.has_value())
      return;
    auto s = (*this->f_)();
    if (!s.has_value())
      return;
    this->publish_state(*s);
  }

  Trigger<> *get_turn_on_trigger() const { return this->turn_on_trigger_; }
  Trigger<> *get_turn_off_trigger() const { return this->turn_off_trigger_; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }
  void set_assumed_state(bool assumed_state) { this->assumed_state_ = assumed_state; }

  float get_setup_priority() const override { return setup_priority::HARDWARE - 2.0f; }

 protected:
  bool assumed_state() override { return this->assumed_state_; }

  void write_state(bool state) override;

  optional<F> f_;
  bool optimistic_{false};
  bool assumed_state_{false};
  Trigger<> *turn_on_trigger_;
  Trigger<> *turn_off_trigger_;
  Trigger<> *prev_trigger_{nullptr};
};

class TemplateSwitch : public TemplateSwitchBase<std::function<optional<bool>()>> {
 public:
  void set_state_lambda(std::function<optional<bool>()> &&f) { this->f_ = f; }
};

/** Optimized template switch for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateSwitch : public TemplateSwitchBase<optional<bool> (*)()> {
 public:
  explicit StatelessTemplateSwitch(optional<bool> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

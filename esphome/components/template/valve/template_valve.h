#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/valve/valve.h"

namespace esphome {
namespace template_ {

enum TemplateValveRestoreMode {
  VALVE_NO_RESTORE,
  VALVE_RESTORE,
  VALVE_RESTORE_AND_CALL,
};

template<typename F> class TemplateValveBase : public valve::Valve, public Component {
 public:
  TemplateValveBase()
      : open_trigger_(new Trigger<>()),
        close_trigger_(new Trigger<>()),
        stop_trigger_(new Trigger<>()),
        toggle_trigger_(new Trigger<>()),
        position_trigger_(new Trigger<float>()) {}

  void loop() override {
    bool changed = false;

    if (this->state_f_.has_value()) {
      auto s = (*this->state_f_)();
      if (s.has_value()) {
        auto pos = clamp(*s, 0.0f, 1.0f);
        if (pos != this->position) {
          this->position = pos;
          changed = true;
        }
      }
    }

    if (changed)
      this->publish_state();
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  Trigger<> *get_open_trigger() const { return this->open_trigger_; }
  Trigger<> *get_close_trigger() const { return this->close_trigger_; }
  Trigger<> *get_stop_trigger() const { return this->stop_trigger_; }
  Trigger<> *get_toggle_trigger() const { return this->toggle_trigger_; }
  Trigger<float> *get_position_trigger() const { return this->position_trigger_; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }
  void set_assumed_state(bool assumed_state) { this->assumed_state_ = assumed_state; }
  void set_has_stop(bool has_stop) { this->has_stop_ = has_stop; }
  void set_has_position(bool has_position) { this->has_position_ = has_position; }
  void set_has_toggle(bool has_toggle) { this->has_toggle_ = has_toggle; }
  void set_restore_mode(TemplateValveRestoreMode restore_mode) { restore_mode_ = restore_mode; }

 protected:
  void control(const valve::ValveCall &call) override;
  valve::ValveTraits get_traits() override;
  void stop_prev_trigger_();

  TemplateValveRestoreMode restore_mode_{VALVE_NO_RESTORE};
  optional<F> state_f_;
  bool assumed_state_{false};
  bool optimistic_{false};
  Trigger<> *open_trigger_;
  Trigger<> *close_trigger_;
  bool has_stop_{false};
  bool has_toggle_{false};
  Trigger<> *stop_trigger_;
  Trigger<> *toggle_trigger_;
  Trigger<> *prev_command_trigger_{nullptr};
  Trigger<float> *position_trigger_;
  bool has_position_{false};
};

class TemplateValve : public TemplateValveBase<std::function<optional<float>()>> {
 public:
  void set_state_lambda(std::function<optional<float>()> &&f) { this->state_f_ = f; }
};

/** Optimized template valve for stateless lambdas (no capture).
 *
 * Uses function pointers instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function) per lambda.
 */
class StatelessTemplateValve : public TemplateValveBase<optional<float> (*)()> {
 public:
  explicit StatelessTemplateValve(optional<float> (*f)()) { this->state_f_ = f; }
};

}  // namespace template_
}  // namespace esphome

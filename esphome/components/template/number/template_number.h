#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateNumberBase : public number::Number, public PollingComponent {
 public:
  TemplateNumberBase() : set_trigger_(new Trigger<float>()) {}

  void setup() override;
  void dump_config() override;

  void update() override {
    if (!this->f_.has_value())
      return;
    auto val = (*this->f_)();
    if (!val.has_value())
      return;
    this->publish_state(*val);
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  Trigger<float> *get_set_trigger() const { return set_trigger_; }
  void set_optimistic(bool optimistic) { optimistic_ = optimistic; }
  void set_initial_value(float initial_value) { initial_value_ = initial_value; }
  void set_restore_value(bool restore_value) { this->restore_value_ = restore_value; }

 protected:
  void control(float value) override;
  bool optimistic_{false};
  float initial_value_{NAN};
  bool restore_value_{false};
  Trigger<float> *set_trigger_;
  optional<F> f_;

  ESPPreferenceObject pref_;
};

class TemplateNumber : public TemplateNumberBase<std::function<optional<float>()>> {
 public:
  void set_template(std::function<optional<float>()> &&f) { this->f_ = f; }
};

/** Optimized template number for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateNumber : public TemplateNumberBase<optional<float> (*)()> {
 public:
  explicit StatelessTemplateNumber(optional<float> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

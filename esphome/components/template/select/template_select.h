#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateSelectBase : public select::Select, public PollingComponent {
 public:
  TemplateSelectBase() : set_trigger_(new Trigger<std::string>()) {}

  void setup() override;
  void dump_config() override;

  void update() override {
    if (!this->f_.has_value())
      return;
    auto val = (*this->f_)();
    if (!val.has_value())
      return;
    if (!this->has_option(*val)) {
      ESP_LOGE("template.select", "Lambda returned an invalid option: %s", (*val).c_str());
      return;
    }
    this->publish_state(*val);
  }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  Trigger<std::string> *get_set_trigger() const { return this->set_trigger_; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }
  void set_initial_option_index(size_t initial_option_index) { this->initial_option_index_ = initial_option_index; }
  void set_restore_value(bool restore_value) { this->restore_value_ = restore_value; }

 protected:
  void control(const std::string &value) override;
  bool optimistic_ = false;
  size_t initial_option_index_{0};
  bool restore_value_ = false;
  Trigger<std::string> *set_trigger_;
  optional<F> f_;

  ESPPreferenceObject pref_;
};

class TemplateSelect : public TemplateSelectBase<std::function<optional<std::string>()>> {
 public:
  void set_template(std::function<optional<std::string>()> &&f) { this->f_ = f; }
};

/** Optimized template select for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateSelect : public TemplateSelectBase<optional<std::string> (*)()> {
 public:
  explicit StatelessTemplateSelect(optional<std::string> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

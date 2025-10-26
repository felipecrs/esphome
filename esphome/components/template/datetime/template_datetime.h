#pragma once

#include "esphome/core/defines.h"

#ifdef USE_DATETIME_DATETIME

#include "esphome/components/datetime/datetime_entity.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/time.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateDateTimeBase : public datetime::DateTimeEntity, public PollingComponent {
 public:
  void update() override {
    if (!this->f_.has_value())
      return;

    auto val = (*this->f_)();
    if (!val.has_value())
      return;

    this->year_ = val->year;
    this->month_ = val->month;
    this->day_ = val->day_of_month;
    this->hour_ = val->hour;
    this->minute_ = val->minute;
    this->second_ = val->second;
    this->publish_state();
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  Trigger<ESPTime> *get_set_trigger() const { return this->set_trigger_; }
  void set_optimistic(bool optimistic) { this->optimistic_ = optimistic; }

  void set_initial_value(ESPTime initial_value) { this->initial_value_ = initial_value; }
  void set_restore_value(bool restore_value) { this->restore_value_ = restore_value; }

 protected:
  void control(const datetime::DateTimeCall &call) override;

  bool optimistic_{false};
  ESPTime initial_value_{};
  bool restore_value_{false};
  Trigger<ESPTime> *set_trigger_ = new Trigger<ESPTime>();
  optional<F> f_;

  ESPPreferenceObject pref_;
};

class TemplateDateTime : public TemplateDateTimeBase<std::function<optional<ESPTime>()>> {
 public:
  void set_template(std::function<optional<ESPTime>()> &&f) { this->f_ = f; }
};

/** Optimized template datetime for stateless lambdas (no capture).
 *
 * Uses function pointers instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function) per lambda.
 */
class StatelessTemplateDateTime : public TemplateDateTimeBase<optional<ESPTime> (*)()> {
 public:
  explicit StatelessTemplateDateTime(optional<ESPTime> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

#endif  // USE_DATETIME_DATETIME

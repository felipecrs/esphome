#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace template_ {

template<typename F> class TemplateBinarySensorBase : public Component, public binary_sensor::BinarySensor {
 public:
  void setup() override { this->loop(); }

  void loop() override {
    if (this->f_ == nullptr)
      return;
    auto s = this->f_();
    if (s.has_value()) {
      this->publish_state(*s);
    }
  }

  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  F f_;
};

class TemplateBinarySensor : public TemplateBinarySensorBase<std::function<optional<bool>()>> {
 public:
  TemplateBinarySensor() { this->f_ = nullptr; }
  void set_template(std::function<optional<bool>()> &&f) { this->f_ = f; }
};

/** Optimized template binary sensor for stateless lambdas (no capture).
 *
 * Uses function pointer instead of std::function to reduce memory overhead.
 * Memory: 4 bytes (function pointer on 32-bit) vs 32 bytes (std::function).
 */
class StatelessTemplateBinarySensor : public TemplateBinarySensorBase<optional<bool> (*)()> {
 public:
  explicit StatelessTemplateBinarySensor(optional<bool> (*f)()) { this->f_ = f; }
};

}  // namespace template_
}  // namespace esphome

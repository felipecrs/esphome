#include "template_valve.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

using namespace esphome::valve;

static const char *const TAG = "template.valve";

// Template instantiations
template<typename F> void TemplateValveBase<F>::setup() {
  switch (this->restore_mode_) {
    case VALVE_NO_RESTORE:
      break;
    case VALVE_RESTORE: {
      auto restore = this->restore_state_();
      if (restore.has_value())
        restore->apply(this);
      break;
    }
    case VALVE_RESTORE_AND_CALL: {
      auto restore = this->restore_state_();
      if (restore.has_value()) {
        restore->to_call(this).perform();
      }
      break;
    }
  }
}

template<typename F> void TemplateValveBase<F>::dump_config() {
  LOG_VALVE("", "Template Valve", this);
  ESP_LOGCONFIG(TAG,
                "  Has position: %s\n"
                "  Optimistic: %s",
                YESNO(this->has_position_), YESNO(this->optimistic_));
}

template<typename F> void TemplateValveBase<F>::control(const ValveCall &call) {
  if (call.get_stop()) {
    this->stop_prev_trigger_();
    this->stop_trigger_->trigger();
    this->prev_command_trigger_ = this->stop_trigger_;
    this->publish_state();
  }
  if (call.get_toggle().has_value()) {
    this->stop_prev_trigger_();
    this->toggle_trigger_->trigger();
    this->prev_command_trigger_ = this->toggle_trigger_;
    this->publish_state();
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    this->stop_prev_trigger_();

    if (pos == VALVE_OPEN) {
      this->open_trigger_->trigger();
      this->prev_command_trigger_ = this->open_trigger_;
    } else if (pos == VALVE_CLOSED) {
      this->close_trigger_->trigger();
      this->prev_command_trigger_ = this->close_trigger_;
    } else {
      this->position_trigger_->trigger(pos);
    }

    if (this->optimistic_) {
      this->position = pos;
    }
  }

  this->publish_state();
}

template<typename F> valve::ValveTraits TemplateValveBase<F>::get_traits() {
  auto traits = ValveTraits();
  traits.set_is_assumed_state(this->assumed_state_);
  traits.set_supports_stop(this->has_stop_);
  traits.set_supports_toggle(this->has_toggle_);
  traits.set_supports_position(this->has_position_);
  return traits;
}

template<typename F> void TemplateValveBase<F>::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

template class TemplateValveBase<std::function<optional<float>()>>;
template class TemplateValveBase<optional<float> (*)()>;

}  // namespace template_
}  // namespace esphome

#include "template_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

static const char *const TAG = "template.switch";

// Template instantiations
template<typename F> void TemplateSwitchBase<F>::write_state(bool state) {
  if (this->prev_trigger_ != nullptr) {
    this->prev_trigger_->stop_action();
  }

  if (state) {
    this->prev_trigger_ = this->turn_on_trigger_;
    this->turn_on_trigger_->trigger();
  } else {
    this->prev_trigger_ = this->turn_off_trigger_;
    this->turn_off_trigger_->trigger();
  }

  if (this->optimistic_)
    this->publish_state(state);
}

template<typename F> void TemplateSwitchBase<F>::setup() {
  optional<bool> initial_state = this->get_initial_state_with_restore_mode();

  if (initial_state.has_value()) {
    ESP_LOGD(TAG, "  Restored state %s", ONOFF(initial_state.value()));
    // if it has a value, restore_mode is not "DISABLED", therefore act on the switch:
    if (initial_state.value()) {
      this->turn_on();
    } else {
      this->turn_off();
    }
  }
}

template<typename F> void TemplateSwitchBase<F>::dump_config() {
  LOG_SWITCH("", "Template Switch", this);
  ESP_LOGCONFIG(TAG, "  Optimistic: %s", YESNO(this->optimistic_));
}

template class TemplateSwitchBase<std::function<optional<bool>()>>;
template class TemplateSwitchBase<optional<bool> (*)()>;

}  // namespace template_
}  // namespace esphome

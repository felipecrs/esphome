#include "template_cover.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

using namespace esphome::cover;

static const char *const TAG = "template.cover";

// Template instantiations
template<typename StateF, typename TiltF> void TemplateCoverBase<StateF, TiltF>::setup() {
  switch (this->restore_mode_) {
    case COVER_NO_RESTORE:
      break;
    case COVER_RESTORE: {
      auto restore = this->restore_state_();
      if (restore.has_value())
        restore->apply(this);
      break;
    }
    case COVER_RESTORE_AND_CALL: {
      auto restore = this->restore_state_();
      if (restore.has_value()) {
        restore->to_call(this).perform();
      }
      break;
    }
  }
}

template<typename StateF, typename TiltF> void TemplateCoverBase<StateF, TiltF>::dump_config() {
  LOG_COVER("", "Template Cover", this);
}

template<typename StateF, typename TiltF> void TemplateCoverBase<StateF, TiltF>::control(const CoverCall &call) {
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

    if (pos == COVER_OPEN) {
      this->open_trigger_->trigger();
      this->prev_command_trigger_ = this->open_trigger_;
    } else if (pos == COVER_CLOSED) {
      this->close_trigger_->trigger();
      this->prev_command_trigger_ = this->close_trigger_;
    } else {
      this->position_trigger_->trigger(pos);
    }

    if (this->optimistic_) {
      this->position = pos;
    }
  }

  if (call.get_tilt().has_value()) {
    auto tilt = *call.get_tilt();
    this->tilt_trigger_->trigger(tilt);

    if (this->optimistic_) {
      this->tilt = tilt;
    }
  }

  this->publish_state();
}

template<typename StateF, typename TiltF> CoverTraits TemplateCoverBase<StateF, TiltF>::get_traits() {
  auto traits = CoverTraits();
  traits.set_is_assumed_state(this->assumed_state_);
  traits.set_supports_stop(this->has_stop_);
  traits.set_supports_toggle(this->has_toggle_);
  traits.set_supports_position(this->has_position_);
  traits.set_supports_tilt(this->has_tilt_);
  return traits;
}

template<typename StateF, typename TiltF> void TemplateCoverBase<StateF, TiltF>::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

template class TemplateCoverBase<std::function<optional<float>()>, std::function<optional<float>()>>;
template class TemplateCoverBase<optional<float> (*)(), optional<float> (*)()>;

}  // namespace template_
}  // namespace esphome

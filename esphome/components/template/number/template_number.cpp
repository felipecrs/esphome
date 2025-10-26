#include "template_number.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

static const char *const TAG = "template.number";

// Template instantiations
template<typename F> void TemplateNumberBase<F>::setup() {
  if (this->f_.has_value())
    return;

  float value;
  if (!this->restore_value_) {
    value = this->initial_value_;
  } else {
    this->pref_ = global_preferences->make_preference<float>(this->get_preference_hash());
    if (!this->pref_.load(&value)) {
      if (!std::isnan(this->initial_value_)) {
        value = this->initial_value_;
      } else {
        value = this->traits.get_min_value();
      }
    }
  }
  this->publish_state(value);
}

template<typename F> void TemplateNumberBase<F>::control(float value) {
  this->set_trigger_->trigger(value);

  if (this->optimistic_)
    this->publish_state(value);

  if (this->restore_value_)
    this->pref_.save(&value);
}

template<typename F> void TemplateNumberBase<F>::dump_config() {
  LOG_NUMBER("", "Template Number", this);
  ESP_LOGCONFIG(TAG, "  Optimistic: %s", YESNO(this->optimistic_));
  LOG_UPDATE_INTERVAL(this);
}

template class TemplateNumberBase<std::function<optional<float>()>>;
template class TemplateNumberBase<optional<float> (*)()>;

}  // namespace template_
}  // namespace esphome

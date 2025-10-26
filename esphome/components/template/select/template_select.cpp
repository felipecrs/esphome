#include "template_select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

static const char *const TAG = "template.select";

// Template instantiations
template<typename F> void TemplateSelectBase<F>::setup() {
  if (this->f_.has_value())
    return;

  size_t index = this->initial_option_index_;
  if (this->restore_value_) {
    this->pref_ = global_preferences->make_preference<size_t>(this->get_preference_hash());
    size_t restored_index;
    if (this->pref_.load(&restored_index) && this->has_index(restored_index)) {
      index = restored_index;
      ESP_LOGD(TAG, "State from restore: %s", this->at(index).value().c_str());
    } else {
      ESP_LOGD(TAG, "State from initial (could not load or invalid stored index): %s", this->at(index).value().c_str());
    }
  } else {
    ESP_LOGD(TAG, "State from initial: %s", this->at(index).value().c_str());
  }

  this->publish_state(this->at(index).value());
}

template<typename F> void TemplateSelectBase<F>::control(const std::string &value) {
  this->set_trigger_->trigger(value);

  if (this->optimistic_)
    this->publish_state(value);

  if (this->restore_value_) {
    auto index = this->index_of(value);
    this->pref_.save(&index.value());
  }
}

template<typename F> void TemplateSelectBase<F>::dump_config() {
  LOG_SELECT("", "Template Select", this);
  LOG_UPDATE_INTERVAL(this);
  if (this->f_.has_value())
    return;
  ESP_LOGCONFIG(TAG,
                "  Optimistic: %s\n"
                "  Initial Option: %s\n"
                "  Restore Value: %s",
                YESNO(this->optimistic_), this->at(this->initial_option_index_).value().c_str(),
                YESNO(this->restore_value_));
}

template class TemplateSelectBase<std::function<optional<std::string>()>>;
template class TemplateSelectBase<optional<std::string> (*)()>;

}  // namespace template_
}  // namespace esphome

#include "template_lock.h"
#include "esphome/core/log.h"

namespace esphome {
namespace template_ {

using namespace esphome::lock;

static const char *const TAG = "template.lock";

// Template instantiations
template<typename F> void TemplateLockBase<F>::control(const lock::LockCall &call) {
  if (this->prev_trigger_ != nullptr) {
    this->prev_trigger_->stop_action();
  }

  auto state = *call.get_state();
  if (state == LOCK_STATE_LOCKED) {
    this->prev_trigger_ = this->lock_trigger_;
    this->lock_trigger_->trigger();
  } else if (state == LOCK_STATE_UNLOCKED) {
    this->prev_trigger_ = this->unlock_trigger_;
    this->unlock_trigger_->trigger();
  }

  if (this->optimistic_)
    this->publish_state(state);
}

template<typename F> void TemplateLockBase<F>::open_latch() {
  if (this->prev_trigger_ != nullptr) {
    this->prev_trigger_->stop_action();
  }
  this->prev_trigger_ = this->open_trigger_;
  this->open_trigger_->trigger();
}

template<typename F> void TemplateLockBase<F>::dump_config() {
  LOG_LOCK("", "Template Lock", this);
  ESP_LOGCONFIG(TAG, "  Optimistic: %s", YESNO(this->optimistic_));
}

template class TemplateLockBase<std::function<optional<lock::LockState>()>>;
template class TemplateLockBase<optional<lock::LockState> (*)()>;

}  // namespace template_
}  // namespace esphome

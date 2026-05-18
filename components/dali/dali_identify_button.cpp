#include "esphome.h"

#if defined(USE_ARDUINO) && defined(USE_ESP32) && defined(USE_BUTTON)

#include "dali_identify_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace DALI {

static const char *const TAG = "dali_identify";

void DALIIdentifyButton::dump_config() {
  LOG_BUTTON("", "DALI Identify", this);
  ESP_LOGCONFIG(TAG, "  address: %u", this->address_);
}

void DALIIdentifyButton::press_action() {
  if (this->interface_ == nullptr) {
    ESP_LOGE(TAG, "DALI interface not configured");
    return;
  }
  this->interface_->identify_device(this->address_);
}

}  // namespace DALI
}  // namespace esphome

#endif

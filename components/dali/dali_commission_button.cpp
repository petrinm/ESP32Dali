#include "esphome.h"

#if defined(USE_ARDUINO) && defined(USE_ESP32) && defined(USE_BUTTON)

#include "dali_commission_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace DALI {

static const char *const TAG = "dali_commission";

void DALICommissionButton::dump_config() {
  LOG_BUTTON("", "DALI Commission", this);
  ESP_LOGCONFIG(TAG, "  start_short_address: %u", this->start_short_address_);
  ESP_LOGCONFIG(TAG, "  max_devices: %u", this->max_devices_);
  ESP_LOGCONFIG(TAG, "  assign_addresses: %s", this->assign_addresses_ ? "yes" : "no");
}

void DALICommissionButton::press_action() {
  if (this->interface_ == nullptr) {
    ESP_LOGE(TAG, "DALI interface not configured");
    return;
  }
  ESP_LOGI(TAG, "Starting bus commissioning (this may take several seconds)...");
  this->interface_->commission_unaddressed_gear(this->start_short_address_, this->max_devices_,
                                                 this->assign_addresses_);
}

}  // namespace DALI
}  // namespace esphome

#endif

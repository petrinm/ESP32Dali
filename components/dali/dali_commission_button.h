#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32) && defined(USE_BUTTON)

#include "dali.h"
#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

namespace esphome {
namespace DALI {

class DALICommissionButton : public button::Button, public Component {
 public:
  void set_interface(DALIInterface *interface) { this->interface_ = interface; }
  void set_start_short_address(uint8_t address) { this->start_short_address_ = address; }
  void set_max_devices(uint8_t max_devices) { this->max_devices_ = max_devices; }
  void set_assign_addresses(bool assign) { this->assign_addresses_ = assign; }
  void dump_config() override;

 protected:
  void press_action() override;

  DALIInterface *interface_{nullptr};
  uint8_t start_short_address_{0};
  uint8_t max_devices_{64};
  bool assign_addresses_{true};
};

}  // namespace DALI
}  // namespace esphome

#endif

#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32) && defined(USE_BUTTON)

#include "dali.h"
#include "esphome/components/button/button.h"
#include "esphome/core/component.h"

namespace esphome {
namespace DALI {

class DALIIdentifyButton : public button::Button, public Component {
 public:
  void set_interface(DALIInterface *interface) { this->interface_ = interface; }
  void set_address(uint8_t address) { this->address_ = address; }
  void dump_config() override;

 protected:
  void press_action() override;

  DALIInterface *interface_{nullptr};
  uint8_t address_{0};
};

}  // namespace DALI
}  // namespace esphome

#endif

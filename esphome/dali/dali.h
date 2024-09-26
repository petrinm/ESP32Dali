#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"

#include "esphome/components/esp32/gpio.h"

#include <esp32-hal-timer.h>


namespace esphome {
namespace DALI {


struct DALIState {

  // Current state
  unsigned int state;

  // Manchester coded forward frame
  uint32_t forward_frame;

  // Bit counter
  uint32_t bitcount;

  // Flag to indicate wether a backward frame is expected
  bool waitForAnswer;

  // Received backwarf frame
  uint32_t backward_frame;

  // Transmitter pin
  InternalGPIOPin *tx_pin;

  // Receiver pin
  InternalGPIOPin *rx_pin;

  // Timer
  hw_timer_t *timer;

};


class DALIInterface: public Component {
 public:
  void setup() override;
  void dump_config() override;

  // Search ballasts
  void search();

  //--- Setup functions
  void set_tx_pin(InternalGPIOPin *tx_pin);
  void set_rx_pin(InternalGPIOPin *rx_pin);

  /* */
  void send_forward_frame(uint16_t forward_frame, bool repeat=false);
  void send_forward_frame(uint16_t forward_frame, uint16_t &response);

  /* Send Direct Arc Power Control */
  void send_dapc(uint8_t address, uint8_t power);

  /* Extinguish the lamp immediately without fading. */
  void send_off(uint8_t address);

  /* Dim UP for 200ms at the selected FATE RATE. */
  void send_up(uint8_t address);

  /* Dim DOWN for 200ms at the selected FADE RATE. */
  void send_down(uint8_t address);

  /* Send forward frame*/
  void send_goto_scene(uint8_t address, uint8_t scene);

  /* Send forward frame */
  void send_set_fade_time(uint8_t address, uint8_t fade_time);

  /* Send forward frame */
  void send_set_fade_rate(uint8_t address, uint8_t fade_rate);

  /* Send forward frame*/
  void send_add_to_Scene(uint8_t address, uint8_t scene);

  /* Send forward frame*/
  void send_remove_from_scene(uint8_t address, uint8_t scene);

  /* Send forward frame*/
  void send_add_to_group(uint8_t address, uint8_t group);

  /* Send forward frame*/
  void send_remove_from_group(uint8_t address, uint8_t group);

protected:
  //--- Defined GPIO Pins

  DALIState driver_;

  int calcDAPC_(double percent);
  double calcPercent_(int DAPC);
  void DALI_Send(uint16_t forward_frame);

  //static uint32_t DALI_Encode(uint16_t forwardFrame);
  //static bool DALI_Decode(uint32_t rawData, uint16_t *pBackwardFrame);
};


}  // namespace DALI
}  // namespace esphome

#endif

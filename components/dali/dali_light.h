#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "dali.h"

namespace esphome {
namespace DALI {

/**
 * @brief ESPHome light platform for a single DALI control gear.
 *
 * Drives brightness via DAPC (IEC 62386-102). When cold/warm white colour
 * temperature limits are configured, also advertises @c COLOR_TEMPERATURE
 * and drives DT8 @ref DALIInterface::set_cct.
 *
 * Bus I/O is deferred to @ref loop at a fixed cadence so @ref write_state
 * stays non-blocking.
 */
class DALILight : public light::LightOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;

  void set_interface(DALIInterface *interface) { interface_ = interface; }

  void set_address(int address) { address_ = address & 0x3F; }
  void set_group(int group) { address_ = 0x40 | (group & 0xF); }
  void set_broadcast() { address_ = 0x7F; }

  void set_fade_time(int fade_time) { fadetime_ = fade_time; }
  void set_fade_rate(int fade_rate) { faderate_ = fade_rate; }

  /** Coldest white point in mireds (from YAML cold_white_color_temperature). */
  void set_min_mireds(float min_mireds) { min_mireds_ = min_mireds; }

  /** Warmest white point in mireds (from YAML warm_white_color_temperature). */
  void set_max_mireds(float max_mireds) { max_mireds_ = max_mireds; }

  void set_color_temperature_enabled(bool enabled) { color_temperature_enabled_ = enabled; }

 protected:
  static constexpr uint32_t MIN_SEND_INTERVAL_MS = 500;

  uint8_t brightness_to_level_(float brightness) const;
  uint16_t ct_to_kelvin_(float color_temperature) const;
  void flush_();

  DALIInterface *interface_{nullptr};

  int address_{0x7F};
  int fadetime_{1};
  int faderate_{2};

  bool color_temperature_enabled_{false};
  float min_mireds_{0};
  float max_mireds_{0};

  uint8_t target_level_{0};
  uint8_t last_sent_level_{0xFF};
  uint16_t target_kelvin_{2700};
  uint16_t last_sent_kelvin_{0};

  bool pending_brightness_{false};
  bool pending_cct_{false};
  uint32_t last_send_ms_{0};
};

}  // namespace DALI
}  // namespace esphome

#endif

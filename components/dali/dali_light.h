#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "esphome/components/light/light_output.h"
#include "esphome/core/component.h"
#include "dali.h"

namespace esphome {
namespace DALI {

/**
 * @brief How tunable-white colour is driven on the bus.
 */
enum class CtMode : uint8_t {
  NONE = 0,          ///< Dim only (DAPC on one address).
  DT8_CT = 1,        ///< Single DT8 gear: DAPC + IEC 62386-209 colour temperature.
  DUAL_DAPC_CT = 2,  ///< Two DAPC channels on separate cold / warm short addresses.
  DT8_RGB = 3,       ///< Single DT8 gear: SET TEMPORARY RGB DIM LEVEL + ACTIVATE.
};

/**
 * @brief ESPHome light platform for a single DALI control gear.
 *
 * Supports plain dimming, DT8 colour temperature or RGB on one address, and
 * dual-channel tunable white (cold + warm on two short addresses, DAPC only).
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

  void set_ct_mode(CtMode mode) { ct_mode_ = mode; }

  void set_min_mireds(float min_mireds) { min_mireds_ = min_mireds; }
  void set_max_mireds(float max_mireds) { max_mireds_ = max_mireds; }

  void set_cold_white_address(int address) { cold_white_address_ = address & 0x3F; }
  void set_warm_white_address(int address) { warm_white_address_ = address & 0x3F; }

 protected:
  static constexpr uint32_t MIN_SEND_INTERVAL_MS = 500;

  uint8_t brightness_to_level_(float brightness) const;
  uint16_t ct_to_kelvin_(float color_temperature) const;
  void write_state_dual_channel_(light::LightState *state);
  void configure_gear_fade_(int address);
  void flush_();

  DALIInterface *interface_{nullptr};

  CtMode ct_mode_{CtMode::NONE};

  int address_{0x7F};
  int cold_white_address_{0};
  int warm_white_address_{1};
  int fadetime_{1};
  int faderate_{2};

  float min_mireds_{0};
  float max_mireds_{0};

  // Single-channel dim or DT8 brightness.
  uint8_t target_level_{0};
  uint8_t last_sent_level_{0xFF};

  // DT8 colour temperature.
  uint16_t target_kelvin_{2700};
  uint16_t last_sent_kelvin_{0};

  // Dual-channel tunable white.
  uint8_t target_cold_level_{0};
  uint8_t target_warm_level_{0};
  uint8_t last_sent_cold_level_{0xFF};
  uint8_t last_sent_warm_level_{0xFF};

  // DT8 RGB.
  uint8_t target_red_level_{0};
  uint8_t target_green_level_{0};
  uint8_t target_blue_level_{0};
  uint8_t last_sent_red_level_{0xFF};
  uint8_t last_sent_green_level_{0xFF};
  uint8_t last_sent_blue_level_{0xFF};

  bool pending_brightness_{false};
  bool pending_cct_{false};
  bool pending_rgb_{false};
  bool pending_cold_{false};
  bool pending_warm_{false};
  uint32_t last_send_ms_{0};
};

}  // namespace DALI
}  // namespace esphome

#endif

#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_transformer.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "dali.h"

namespace esphome {
namespace DALI {

class DALILight;

/**
 * @brief DALI gear-native transition: one bus command, gear ramps over @c length_.
 */
class DALILightTransitionTransformer : public light::LightTransformer {
 public:
  explicit DALILightTransitionTransformer(DALILight *light) : light_(light) {}

  optional<light::LightColorValues> apply() override;

  bool is_finished() override { return this->get_progress_() >= 1.0f; }

 protected:
  DALILight *light_;
  bool sent_{false};
};

/**
 * @brief How tunable-white colour is driven on the bus.
 */
enum class ColorMode : uint8_t {
  NONE = 0,          ///< Dim only (DAPC on one address).
  DT8_CT = 1,        ///< Single DT8 gear: DAPC + IEC 62386-209 colour temperature.
  DUAL_DAPC_CT = 2,  ///< Two DAPC channels on separate cold / warm short addresses.
  DT8_RGB = 3,       ///< Single DT8 gear: SET TEMPORARY RGB DIM LEVEL + ACTIVATE.
};

/**
 * @brief ESPHome light platform for a single DALI control gear.
 *
 * Fade duration comes from ESPHome transitions (@c transition_length /
 * @c default_transition_length) in milliseconds. @ref DALIInterface maps that
 * to standard or extended DALI fade before each arc-power change.
 */
class DALILight : public light::LightOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void setup_state(light::LightState *state) override;
  std::unique_ptr<light::LightTransformer> create_default_transition() override;

  void set_interface(DALIInterface *interface) { interface_ = interface; }

  void set_address(int address) { address_ = address & 0x3F; }
  void set_group(int group) { address_ = 0x40 | (group & 0xF); }
  void set_broadcast() { address_ = 0x7F; }

  void set_color_mode(ColorMode mode) { color_mode_ = mode; }

  void set_min_mireds(float min_mireds) { min_mireds_ = min_mireds; }
  void set_max_mireds(float max_mireds) { max_mireds_ = max_mireds; }

  void set_cold_white_address(int address) { cold_white_address_ = address & 0x3F; }
  void set_warm_white_address(int address) { warm_white_address_ = address & 0x3F; }

  /** Called by @ref DALILightTransitionTransformer on transition start. */
  void write_state_for_transition_(const light::LightColorValues &target, uint32_t duration_ms);

 protected:
  static constexpr uint32_t MIN_SEND_INTERVAL_MS = 500;

  uint8_t brightness_to_level_(float brightness) const;
  uint16_t ct_to_kelvin_(float color_temperature) const;
  void populate_targets_from_values_(const light::LightColorValues &values);
  void write_state_dual_channel_(const light::LightColorValues &values);
  void configure_fade_if_needed_(int address);
  void flush_();

  DALIInterface *interface_{nullptr};
  light::LightState *light_state_{nullptr};

  ColorMode color_mode_{ColorMode::NONE};

  int address_{0x7F};
  int cold_white_address_{0};
  int warm_white_address_{1};

  float min_mireds_{0};
  float max_mireds_{0};

  uint32_t pending_fade_duration_ms_{0};
  int last_fade_address_{-1};
  uint32_t last_fade_duration_ms_{UINT32_MAX};

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

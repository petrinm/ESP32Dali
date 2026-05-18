#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "dali_light.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/log.h"

#include <math.h>

namespace esphome {
namespace DALI {

static const char *TAG = "dali_light";

light::LightTraits DALILight::get_traits() {
  light::LightTraits traits;
  switch (this->ct_mode_) {
    case CtMode::DT8:
    case CtMode::DUAL_CHANNEL:
      traits.set_supported_color_modes(
          {light::ColorMode::COLOR_TEMPERATURE, light::ColorMode::COLD_WARM_WHITE});
      traits.set_min_mireds(this->min_mireds_);
      traits.set_max_mireds(this->max_mireds_);
      break;
    case CtMode::NONE:
    default:
      traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
      break;
  }
  return traits;
}

void DALILight::configure_gear_fade_(int address) {
  this->interface_->set_fade_time(address, (uint8_t) this->fadetime_);
  this->interface_->set_fade_rate(address, (uint8_t) this->faderate_);
}

void DALILight::setup() {
  if (this->ct_mode_ == CtMode::DUAL_CHANNEL) {
    ESP_LOGD(TAG, "Configuring dual-channel CT: cold=0x%02X warm=0x%02X fade_time=%d fade_rate=%d",
             this->cold_white_address_, this->warm_white_address_, this->fadetime_, this->faderate_);
    this->configure_gear_fade_(this->cold_white_address_);
    this->configure_gear_fade_(this->warm_white_address_);
    return;
  }

  ESP_LOGD(TAG, "Configuring gear at 0x%02X: fade_time=%d, fade_rate=%d", this->address_, this->fadetime_,
           this->faderate_);
  this->configure_gear_fade_(this->address_);
}

void DALILight::loop() {
  if (!this->pending_brightness_ && !this->pending_cct_ && !this->pending_cold_ && !this->pending_warm_) {
    return;
  }

  const uint32_t now = millis();
  if (now - this->last_send_ms_ < MIN_SEND_INTERVAL_MS) {
    return;
  }

  this->flush_();
  this->last_send_ms_ = now;
}

void DALILight::dump_config() {
  ESP_LOGCONFIG(TAG, "DALILight");
  switch (this->ct_mode_) {
    case CtMode::DUAL_CHANNEL:
      ESP_LOGCONFIG(TAG, "  mode: dual-channel CT");
      ESP_LOGCONFIG(TAG, "  cold_white_address: 0x%02X", this->cold_white_address_);
      ESP_LOGCONFIG(TAG, "  warm_white_address: 0x%02X", this->warm_white_address_);
      break;
    case CtMode::DT8:
      ESP_LOGCONFIG(TAG, "  mode: DT8 colour temperature");
      ESP_LOGCONFIG(TAG, "  address: 0x%02X", this->address_);
      break;
    default:
      ESP_LOGCONFIG(TAG, "  mode: brightness");
      ESP_LOGCONFIG(TAG, "  address: 0x%02X", this->address_);
      break;
  }
  ESP_LOGCONFIG(TAG, "  fade_rate: %d", this->faderate_);
  ESP_LOGCONFIG(TAG, "  fade_time: %d", this->fadetime_);
  ESP_LOGCONFIG(TAG, "  min send interval: %u ms", (unsigned) MIN_SEND_INTERVAL_MS);
  if (this->ct_mode_ != CtMode::NONE) {
    ESP_LOGCONFIG(TAG, "  color_temperature: %.1f - %.1f mireds", this->min_mireds_, this->max_mireds_);
  }
}

uint8_t DALILight::brightness_to_level_(float brightness) const {
  int level = (int) lroundf(brightness * 254.0f);
  if (level < 0) {
    level = 0;
  }
  if (level > 254) {
    level = 254;
  }
  return (uint8_t) level;
}

uint16_t DALILight::ct_to_kelvin_(float color_temperature) const {
  const float mired = this->min_mireds_ + color_temperature * (this->max_mireds_ - this->min_mireds_);
  if (mired <= 0.0f) {
    return 1000;
  }
  const float kelvin = 1000000.0f / mired;
  if (kelvin < 1000.0f) {
    return 1000;
  }
  if (kelvin > 20000.0f) {
    return 20000;
  }
  return (uint16_t) lroundf(kelvin);
}

void DALILight::write_state_dual_channel_(light::LightState *state) {
  float cold = 0.0f;
  float warm = 0.0f;

  const auto color_mode = state->current_values.get_color_mode();
  if (color_mode == light::ColorMode::COLD_WARM_WHITE) {
    state->current_values_as_cwww(&cold, &warm, false);
  } else {
    // COLOR_TEMPERATURE (and transitions): map CT slider to channel mix.
    float color_temperature = 0.0f;
    float brightness = 0.0f;
    state->current_values_as_ct(&color_temperature, &brightness);
    cold = brightness * (1.0f - color_temperature);
    warm = brightness * color_temperature;
  }

  this->target_cold_level_ = this->brightness_to_level_(cold);
  this->target_warm_level_ = this->brightness_to_level_(warm);
  this->pending_cold_ = true;
  this->pending_warm_ = true;

  ESP_LOGV(TAG, "write_state dual: cold=%.3f warm=%.3f -> levels %u/%u", cold, warm, this->target_cold_level_,
           this->target_warm_level_);
}

void DALILight::write_state(light::LightState *state) {
  if (this->ct_mode_ == CtMode::DUAL_CHANNEL) {
    this->write_state_dual_channel_(state);
    return;
  }

  if (this->ct_mode_ == CtMode::DT8) {
    float color_temperature;
    float brightness;
    state->current_values_as_ct(&color_temperature, &brightness);

    this->target_level_ = this->brightness_to_level_(brightness);
    this->target_kelvin_ = this->ct_to_kelvin_(color_temperature);
    this->pending_brightness_ = true;
    this->pending_cct_ = true;

    ESP_LOGV(TAG, "write_state DT8: ct=%.3f brightness=%.3f -> level=%u kelvin=%u", color_temperature, brightness,
             this->target_level_, this->target_kelvin_);
    return;
  }

  float brightness;
  state->current_values_as_brightness(&brightness);

  this->target_level_ = this->brightness_to_level_(brightness);
  this->pending_brightness_ = true;
  this->pending_cct_ = false;

  ESP_LOGV(TAG, "write_state: brightness=%.3f -> level=%u", brightness, this->target_level_);
}

void DALILight::flush_() {
  if (this->pending_cold_) {
    const uint8_t level = this->target_cold_level_;
    this->pending_cold_ = false;

    if (level != this->last_sent_cold_level_) {
      ESP_LOGD(TAG, "DAPC cold -> level=%u addr=0x%02X", level, this->cold_white_address_);
      this->interface_->send_dapc(this->cold_white_address_, level);
      this->last_sent_cold_level_ = level;
    }
  }

  if (this->pending_warm_) {
    const uint8_t level = this->target_warm_level_;
    this->pending_warm_ = false;

    if (level != this->last_sent_warm_level_) {
      ESP_LOGD(TAG, "DAPC warm -> level=%u addr=0x%02X", level, this->warm_white_address_);
      this->interface_->send_dapc(this->warm_white_address_, level);
      this->last_sent_warm_level_ = level;
    }
  }

  if (this->pending_brightness_) {
    const uint8_t level = this->target_level_;
    this->pending_brightness_ = false;

    if (level != this->last_sent_level_) {
      ESP_LOGD(TAG, "DAPC -> level=%u addr=0x%02X", level, this->address_);
      this->interface_->send_dapc(this->address_, level);
      this->last_sent_level_ = level;
    }
  }

  if (this->pending_cct_ && this->ct_mode_ == CtMode::DT8) {
    const uint16_t kelvin = this->target_kelvin_;
    this->pending_cct_ = false;

    if (kelvin != this->last_sent_kelvin_) {
      ESP_LOGD(TAG, "set_cct -> %u K addr=0x%02X", kelvin, this->address_);
      this->interface_->set_cct(this->address_, kelvin);
      this->last_sent_kelvin_ = kelvin;
    }
  }
}

}  // namespace DALI
}  // namespace esphome

#endif

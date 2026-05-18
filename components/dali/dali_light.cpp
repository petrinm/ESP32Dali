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
  if (this->color_temperature_enabled_) {
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->min_mireds_);
    traits.set_max_mireds(this->max_mireds_);
  } else {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  }
  return traits;
}

void DALILight::setup() {
  ESP_LOGD(TAG, "Configuring gear at 0x%02X: fade_time=%d, fade_rate=%d",
           address_, fadetime_, faderate_);
  interface_->set_fade_time(address_, (uint8_t) fadetime_);
  interface_->set_fade_rate(address_, (uint8_t) faderate_);
}

void DALILight::loop() {
  if (!this->pending_brightness_ && !this->pending_cct_) {
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
  ESP_LOGCONFIG(TAG, "  address: 0x%02X", address_);
  ESP_LOGCONFIG(TAG, "  fade_rate: %d", faderate_);
  ESP_LOGCONFIG(TAG, "  fade_time: %d", fadetime_);
  ESP_LOGCONFIG(TAG, "  min send interval: %u ms", (unsigned) MIN_SEND_INTERVAL_MS);
  if (this->color_temperature_enabled_) {
    ESP_LOGCONFIG(TAG, "  color_temperature: %.1f - %.1f mireds", min_mireds_, max_mireds_);
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

void DALILight::write_state(light::LightState *state) {
  if (this->color_temperature_enabled_) {
    float color_temperature;
    float brightness;
    state->current_values_as_ct(&color_temperature, &brightness);

    this->target_level_ = this->brightness_to_level_(brightness);
    this->target_kelvin_ = this->ct_to_kelvin_(color_temperature);
    this->pending_brightness_ = true;
    this->pending_cct_ = true;

    ESP_LOGV(TAG, "write_state: ct=%.3f brightness=%.3f -> level=%u kelvin=%u",
             color_temperature, brightness, target_level_, target_kelvin_);
  } else {
    float brightness;
    state->current_values_as_brightness(&brightness);

    this->target_level_ = this->brightness_to_level_(brightness);
    this->pending_brightness_ = true;
    this->pending_cct_ = false;

    ESP_LOGV(TAG, "write_state: brightness=%.3f -> level=%u", brightness, target_level_);
  }
}

void DALILight::flush_() {
  if (this->pending_brightness_) {
    const uint8_t level = this->target_level_;
    this->pending_brightness_ = false;

    if (level != this->last_sent_level_) {
      ESP_LOGD(TAG, "DAPC -> level=%u addr=0x%02X", level, address_);
      this->interface_->send_dapc(address_, level);
      this->last_sent_level_ = level;
    }
  }

  if (this->pending_cct_ && this->color_temperature_enabled_) {
    const uint16_t kelvin = this->target_kelvin_;
    this->pending_cct_ = false;

    if (kelvin != this->last_sent_kelvin_) {
      ESP_LOGD(TAG, "set_cct -> %u K addr=0x%02X", kelvin, address_);
      this->interface_->set_cct(address_, kelvin);
      this->last_sent_kelvin_ = kelvin;
    }
  }
}

}  // namespace DALI
}  // namespace esphome

#endif

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "dali_light.h"
#include "esphome/components/light/light_color_values.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <math.h>

namespace esphome {
namespace DALI {

static const char *TAG = "dali_light";

optional<light::LightColorValues> DALILightTransitionTransformer::apply() {
  if (!this->sent_) {
    this->sent_ = true;
    this->light_->write_state_for_transition_(this->get_target_values(), this->length_);
    return this->get_target_values();
  }
  return {};
}

light::LightTraits DALILight::get_traits() {
  light::LightTraits traits;
  switch (this->ct_mode_) {
    case CtMode::DT8_CT:
    case CtMode::DUAL_DAPC_CT:
      traits.set_supported_color_modes(
          {light::ColorMode::COLOR_TEMPERATURE, light::ColorMode::COLD_WARM_WHITE});
      traits.set_min_mireds(this->min_mireds_);
      traits.set_max_mireds(this->max_mireds_);
      break;
    case CtMode::DT8_RGB:
      traits.set_supported_color_modes({light::ColorMode::RGB});
      break;
    case CtMode::NONE:
    default:
      traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
      break;
  }
  return traits;
}

void DALILight::setup() {}

void DALILight::setup_state(light::LightState *state) { this->light_state_ = state; }

std::unique_ptr<light::LightTransformer> DALILight::create_default_transition() {
  return make_unique<DALILightTransitionTransformer>(this);
}

void DALILight::loop() {
  if (!this->pending_brightness_ && !this->pending_cct_ && !this->pending_rgb_ && !this->pending_cold_ &&
      !this->pending_warm_) {
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
    case CtMode::DUAL_DAPC_CT:
      ESP_LOGCONFIG(TAG, "  mode: dual DAPC CT");
      ESP_LOGCONFIG(TAG, "  cold_white_address: 0x%02X", this->cold_white_address_);
      ESP_LOGCONFIG(TAG, "  warm_white_address: 0x%02X", this->warm_white_address_);
      break;
    case CtMode::DT8_CT:
      ESP_LOGCONFIG(TAG, "  mode: DT8 CT");
      ESP_LOGCONFIG(TAG, "  address: 0x%02X", this->address_);
      break;
    case CtMode::DT8_RGB:
      ESP_LOGCONFIG(TAG, "  mode: DT8 RGB");
      ESP_LOGCONFIG(TAG, "  address: 0x%02X", this->address_);
      break;
    default:
      ESP_LOGCONFIG(TAG, "  mode: brightness");
      ESP_LOGCONFIG(TAG, "  address: 0x%02X", this->address_);
      break;
  }
  ESP_LOGCONFIG(TAG, "  min send interval: %u ms", (unsigned) MIN_SEND_INTERVAL_MS);
  if (this->ct_mode_ == CtMode::DT8_CT || this->ct_mode_ == CtMode::DUAL_DAPC_CT) {
    ESP_LOGCONFIG(TAG, "  color_temperature: %.1f - %.1f mireds", this->min_mireds_, this->max_mireds_);
  }
}

uint8_t DALILight::brightness_to_level_(float brightness) const {
  int level = (int) lroundf(brightness * 254.0f);
  if (level < 0) {
    level = 0;
  } else if (level > 254) {
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
  } else if (kelvin > 20000.0f) {
    return 20000;
  }
  return (uint16_t) lroundf(kelvin);
}

void DALILight::configure_fade_if_needed_(int address) {
  if (this->last_fade_address_ == address && this->last_fade_duration_ms_ == this->pending_fade_duration_ms_) {
    return;
  }
  this->interface_->configure_fade_duration(address, this->pending_fade_duration_ms_);
  this->last_fade_address_ = address;
  this->last_fade_duration_ms_ = this->pending_fade_duration_ms_;
}

void DALILight::write_state_dual_channel_(const light::LightColorValues &values) {
  float cold = 0.0f;
  float warm = 0.0f;

  const auto color_mode = values.get_color_mode();
  if (color_mode == light::ColorMode::COLD_WARM_WHITE) {
    float cw;
    float ww;
    values.as_cwww(&cw, &ww, false);
    cold = this->light_state_->gamma_correct_lut(cw);
    warm = this->light_state_->gamma_correct_lut(ww);
  } else {
    float color_temperature = 0.0f;
    float brightness = 0.0f;
    values.as_ct(this->min_mireds_, this->max_mireds_, &color_temperature, &brightness);
    brightness = this->light_state_->gamma_correct_lut(brightness);
    cold = brightness * (1.0f - color_temperature);
    warm = brightness * color_temperature;
  }

  this->target_cold_level_ = this->brightness_to_level_(cold);
  this->target_warm_level_ = this->brightness_to_level_(warm);
  this->pending_cold_ = true;
  this->pending_warm_ = true;
}

void DALILight::populate_targets_from_values_(const light::LightColorValues &values) {
  this->pending_brightness_ = false;
  this->pending_cct_ = false;
  this->pending_rgb_ = false;
  this->pending_cold_ = false;
  this->pending_warm_ = false;

  if (this->ct_mode_ == CtMode::DUAL_DAPC_CT) {
    this->write_state_dual_channel_(values);
    return;
  }

  if (this->ct_mode_ == CtMode::DT8_RGB) {
    const float color_brightness = values.get_color_brightness();
    const float r = this->light_state_->gamma_correct_lut(values.get_red() * color_brightness);
    const float g = this->light_state_->gamma_correct_lut(values.get_green() * color_brightness);
    const float b = this->light_state_->gamma_correct_lut(values.get_blue() * color_brightness);
    this->target_red_level_ = this->brightness_to_level_(r);
    this->target_green_level_ = this->brightness_to_level_(g);
    this->target_blue_level_ = this->brightness_to_level_(b);
    this->pending_rgb_ = true;
    return;
  }

  if (this->ct_mode_ == CtMode::DT8_CT) {
    float color_temperature = 0.0f;
    float brightness = 0.0f;
    values.as_ct(this->min_mireds_, this->max_mireds_, &color_temperature, &brightness);
    brightness = this->light_state_->gamma_correct_lut(brightness);
    this->target_level_ = this->brightness_to_level_(brightness);
    this->target_kelvin_ = this->ct_to_kelvin_(color_temperature);
    this->pending_brightness_ = true;
    this->pending_cct_ = true;
    return;
  }

  const float brightness = this->light_state_->gamma_correct_lut(values.get_brightness() * values.get_state());
  this->target_level_ = this->brightness_to_level_(brightness);
  this->pending_brightness_ = true;
}

void DALILight::write_state_for_transition_(const light::LightColorValues &target, uint32_t duration_ms) {
  ESP_LOGD(TAG, "transition start: duration=%u ms", (unsigned) duration_ms);
  this->pending_fade_duration_ms_ = duration_ms;
  this->last_fade_address_ = -1;
  this->populate_targets_from_values_(target);
  this->flush_();
  this->last_send_ms_ = millis();
}

void DALILight::write_state(light::LightState *state) {
  this->pending_fade_duration_ms_ = 0;
  this->last_fade_address_ = -1;
  this->populate_targets_from_values_(state->current_values);
}

void DALILight::flush_() {
  if (this->pending_cold_) {
    const uint8_t level = this->target_cold_level_;
    this->pending_cold_ = false;

    if (level != this->last_sent_cold_level_) {
      this->configure_fade_if_needed_(this->cold_white_address_);
      ESP_LOGD(TAG, "DAPC cold -> level=%u addr=0x%02X fade=%u ms", level, this->cold_white_address_,
               (unsigned) this->pending_fade_duration_ms_);
      this->interface_->send_dapc(this->cold_white_address_, level);
      this->last_sent_cold_level_ = level;
    }
  }

  if (this->pending_warm_) {
    const uint8_t level = this->target_warm_level_;
    this->pending_warm_ = false;

    if (level != this->last_sent_warm_level_) {
      this->configure_fade_if_needed_(this->warm_white_address_);
      ESP_LOGD(TAG, "DAPC warm -> level=%u addr=0x%02X fade=%u ms", level, this->warm_white_address_,
               (unsigned) this->pending_fade_duration_ms_);
      this->interface_->send_dapc(this->warm_white_address_, level);
      this->last_sent_warm_level_ = level;
    }
  }

  if (this->pending_brightness_) {
    const uint8_t level = this->target_level_;
    this->pending_brightness_ = false;

    if (level != this->last_sent_level_) {
      this->configure_fade_if_needed_(this->address_);
      ESP_LOGD(TAG, "DAPC -> level=%u addr=0x%02X fade=%u ms", level, this->address_,
               (unsigned) this->pending_fade_duration_ms_);
      this->interface_->send_dapc(this->address_, level);
      this->last_sent_level_ = level;
    }
  }

  if (this->pending_cct_ && this->ct_mode_ == CtMode::DT8_CT) {
    const uint16_t kelvin = this->target_kelvin_;
    this->pending_cct_ = false;

    if (kelvin != this->last_sent_kelvin_) {
      ESP_LOGD(TAG, "set_cct -> %u K addr=0x%02X", kelvin, this->address_);
      this->interface_->set_cct(this->address_, kelvin);
      this->last_sent_kelvin_ = kelvin;
    }
  }

  if (this->pending_rgb_ && this->ct_mode_ == CtMode::DT8_RGB) {
    const uint8_t red = this->target_red_level_;
    const uint8_t green = this->target_green_level_;
    const uint8_t blue = this->target_blue_level_;
    this->pending_rgb_ = false;

    if (red != this->last_sent_red_level_ || green != this->last_sent_green_level_ ||
        blue != this->last_sent_blue_level_) {
      ESP_LOGD(TAG, "set_rgb -> r=%u g=%u b=%u addr=0x%02X", red, green, blue, this->address_);
      this->interface_->set_rgb(this->address_, red, green, blue);
      this->last_sent_red_level_ = red;
      this->last_sent_green_level_ = green;
      this->last_sent_blue_level_ = blue;
    }
  }
}

}  // namespace DALI
}  // namespace esphome

#endif

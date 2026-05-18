#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "dali_output.h"
#include "esphome/core/log.h"

#include <math.h>

namespace esphome {
namespace DALI {

static const char *TAG = "dali_output";

void DALIOutput::setup() {
  // Configure the gear's own fade engine: between our (deliberately
  // infrequent) DAPC frames the gear interpolates from its current
  // level to the new target over `fadetime_` (a DALI fade-time index,
  // 0-15, mapping non-linearly to ~0-90 s). fade_rate is used by the
  // UP/DOWN commands; we set it for completeness even though DALIOutput
  // itself only uses DAPC.
  //
  // Both writes are addressed at this output's specific address (short,
  // group or broadcast). DALIInterface::setup() is guaranteed to have
  // run already because it returns setup_priority::BUS, which is higher
  // than the default this component runs at.
  ESP_LOGD(TAG, "Configuring gear at 0x%02X: fade_time=%d, fade_rate=%d",
           address_, fadetime_, faderate_);
  interface->set_fade_time(address_, (uint8_t) fadetime_);
  interface->set_fade_rate(address_, (uint8_t) faderate_);
}

void DALIOutput::loop() {
  if (!pending_) {
    return;
  }

  const uint32_t now = millis();
  if (now - last_send_ms_ < MIN_SEND_INTERVAL_MS) {
    return;
  }

  // Snapshot the target. Clearing `pending_` here (rather than after
  // send_dapc) is safe because ESPHome runs single-threaded: write_state
  // cannot fire during send_dapc. If a new target arrives after we
  // return, write_state() will set pending_ true again and the next
  // loop iteration will push it.
  const uint8_t level = target_level_;
  pending_ = false;

  // Skip redundant frames - if nothing has changed since the last DAPC
  // we don't need to retransmit. This also lets write_state() be called
  // every loop tick without flooding the bus once a fade settles.
  if (level == last_sent_level_) {
    return;
  }

  ESP_LOGD(TAG, "DAPC -> level=%u addr=0x%02X", level, address_);
  interface->send_dapc(address_, level);

  last_sent_level_ = level;
  last_send_ms_ = now;
}


void DALIOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "DALIOutput");
  ESP_LOGCONFIG(TAG, "  address: 0x%02X", address_);
  ESP_LOGCONFIG(TAG, "  fade_rate: %d", faderate_);
  ESP_LOGCONFIG(TAG, "  fade_time: %d", fadetime_);
  ESP_LOGCONFIG(TAG, "  min send interval: %u ms", (unsigned) MIN_SEND_INTERVAL_MS);
}


void DALIOutput::write_state(float value)
{
  // DAPC level 0xFE is "max", 0xFF is reserved as MASK ("no change").
  // Round and clamp the float [0,1] input to the valid range [0, 254].
  int level = (int) lroundf(value * 254.0f);
  if (level < 0) level = 0;
  if (level > 254) level = 254;

  ESP_LOGV(TAG, "write_state: value=%.3f -> level=%d", value, level);

  // Record the latest target; loop() flushes onto the bus at a slow
  // cadence (MIN_SEND_INTERVAL_MS) and the gear itself does the actual
  // fade between successive DAPC levels via its configured fade time.
  // ESPHome's light component can call write_state() rapidly during a
  // transition - only the most recent target ever reaches the bus, so
  // changing the target mid-fade is naturally absorbed.
  target_level_ = (uint8_t) level;
  pending_ = true;
}


} //namespace DALI
} //namespace esphome

#endif

#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "esphome/core/component.h"
#include "esphome/components/output/float_output.h"
#include "../dali.h"

namespace esphome {
namespace DALI {

class DALIOutput: public output::FloatOutput, public Component {
 public:
  void setup() override;
  void loop() override;
  void write_state(float state) override;
  void dump_config() override;

  void set_interface(DALIInterface *interface_) { interface = interface_; }

  void set_address(int address) { address_ = address & 0x3F; }
  void set_group(int group) { address_ = 0x40 | (group & 0xF); }
  void set_broadcast() { address_ = 0x7F; }

  void set_fade_time(int FadeTime) { fadetime_ = FadeTime; }
  void set_fade_rate(int FadeRate) { faderate_ = FadeRate; }

protected:
  // Minimum gap between two DAPC frames pushed onto the bus, in ms.
  //
  // We deliberately keep this relatively long and rely on the DALI gear
  // itself to interpolate smoothly between successive target levels
  // using its configured fade time (set in setup() via set_fade_time).
  // This means we put far less traffic on the bus than a strict
  // "host drives every pixel" model, and the gear's own
  // anti-flicker / log-corrected fade ramp is what's actually visible.
  //
  // Pick something <= the configured fade time so the gear is still
  // mid-fade when the next DAPC arrives. With fade time ~5 s (DALI
  // default 7 => 5.66 s), 500 ms updates keep us comfortably inside
  // that window. The constant is intentionally not exposed in YAML;
  // tweak it here if you change the default fade time defaults.
  static constexpr uint32_t MIN_SEND_INTERVAL_MS = 500;

  DALIInterface *interface;

  // 7-bit DALI address: 0x00-0x3F short, 0x40-0x4F group, 0x7F broadcast.
  // Default to broadcast so an output with neither address nor group still
  // does something sensible.
  int address_ = 0x7F;
  int fadetime_ = 1;
  int faderate_ = 2;

  // Slow-fade target tracking. write_state() updates target_level_ and
  // sets pending_=true; loop() flushes the latest value to the bus at
  // most once per MIN_SEND_INTERVAL_MS. Successive write_state() calls
  // during a long ESPHome transition therefore collapse into a stream
  // of DAPC frames at our cadence, always carrying the most recent
  // target - which means changing the target mid-fade just works.
  uint8_t target_level_{0};
  uint8_t last_sent_level_{0xFF};  // 0xFF = "nothing sent yet" sentinel
  uint32_t last_send_ms_{0};
  bool pending_{false};
};


} //namespace DALI
} //namespace esphome

#endif

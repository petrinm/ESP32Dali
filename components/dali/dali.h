#pragma once

#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include <stdint.h>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/hal.h"

#include "esphome/components/esp32/gpio.h"

#include <esp32-hal-timer.h>


namespace esphome {
namespace DALI {


/**
 * @brief Persistent low-level state for one DALI bus driver instance.
 *
 * Shared between the host-side @ref DALIInterface methods and the timer /
 * GPIO interrupt service routines. Members modified from ISR context are
 * declared @c volatile so the compiler doesn't cache them across the
 * FreeRTOS yield inside the host wait loop.
 */
struct DALIState {

  /**
   * @brief Current state-machine state.
   *
   * One of the @c MS_* enumerators defined in dali.cpp. Updated by the
   * timer / GPIO ISRs, polled by @ref DALIInterface::do_transfer_.
   */
  volatile unsigned int state;

  /**
   * @brief Manchester-coded forward frame currently being shifted out.
   *
   * Filled by @ref DALIInterface::DALI_Encode and consumed MSB-first by
   * the TX state machine, one half-bit per timer tick.
   */
  uint32_t forward_frame;

  /**
   * @brief Half-bit counter, reused by TX and RX paths.
   *
   * - TX: counts the 32 data half-bits of the forward frame (0..32).
   * - RX: counts the 18 half-bits of an incoming backward frame
   *   (start bit + 8 data bits, 0..18).
   *
   * Reset to zero between the two phases by the state machine.
   */
  volatile uint32_t bitcount;

  /**
   * @brief @c true while a query forward frame's backward-frame window
   *        is open.
   */
  bool wait_for_answer;

  /**
   * @brief Raw captured bi-phase pairs from the most recent backward frame.
   *
   * Built up LSB-first by the GPIO ISR. See @ref DALIInterface::DALI_Decode
   * for the bit layout.
   */
  volatile uint32_t backward_frame;

  /**
   * @brief Host-side TX pin handle. Used only from non-ISR context.
   *
   * Configured by @ref DALIInterface::set_tx_pin.
   */
  InternalGPIOPin *tx_pin;

  /**
   * @brief Host-side RX pin handle.
   *
   * Optional; if @c nullptr, query commands fail with a warning instead
   * of waiting forever.
   */
  InternalGPIOPin *rx_pin;

  /**
   * @brief ISR-safe wrapper around @ref tx_pin.
   *
   * Populated in @ref DALIInterface::setup so the ISRs can read/write
   * the pin without depending on flash-resident GPIO helpers.
   */
  ISRInternalGPIOPin tx_isr_pin;

  /**
   * @brief ISR-safe wrapper around @ref rx_pin.
   *
   * Only valid when @ref rx_pin is non-null.
   */
  ISRInternalGPIOPin rx_isr_pin;

  /**
   * @brief 1 MHz hardware timer driving both the TX half-bit clock and
   *        the RX inter-edge timing measurements.
   */
  hw_timer_t *timer;

};

/**
 * @brief Selector values for @ref DALIInterface::query_colour_value.
 *
 * Mirrors IEC 62386-209 Table 11 (DT8 colour control gears). Pass one
 * of these to QUERY COLOUR VALUE after loading it into DTR0.
 */
enum QueryColourValue
{
  XCoordinate = 0,
  YCoordinate = 1,
  ColourTemperatureTC = 2,
  PrimaryNDimLevel0 = 3,
  PrimaryNDimLevel1 = 4,
  PrimaryNDimLevel2 = 5,
  PrimaryNDimLevel3 = 6,
  PrimaryNDimLevel4 = 7,
  PrimaryNDimLevel5 = 8,
  RedDimLevel = 9,
  GreenDimLevel = 10,
  BlueDimLevel = 11,
  WhiteDimLevel = 12,
  AmberDimLevel = 13,
  FreecolourDimLevel = 14,
  RGBWAFControl = 15,
  XCoordinatePrimaryN0 = 64,
  YCoordinatePrimaryN0 = 65,
  TYPrimaryN0 = 66,
  XCoordinatePrimaryN1 = 67,
  YCoordinatePrimaryN1 = 68,
  TYPrimaryN1 = 69,
  XCoordinatePrimaryN2 = 70,
  YCoordinatePrimaryN2 = 71,
  TYPrimaryN2 = 72,
  XCoordinatePrimaryN3 = 73,
  YCoordinatePrimaryN3 = 74,
  TYPrimaryN3 = 75,
  XCoordinatePrimaryN4 = 76,
  YCoordinatePrimaryN4 = 77,
  TYPrimaryN4 = 78,
  XCoordinatePrimaryN5 = 79,
  YCoordinatePrimaryN5 = 80,
  TYPrimaryN5 = 81,
  NumberOfPrimaries = 82,
  ColourTemperatureTcCoolest = 128,
  ColourTemperatureTcPhysicalCoolest = 129,
  ColourTemperatureTcWarmest = 130,
  ColourTemperatureTcPhysicalWarmest = 131,
  TemporaryXCoordinate = 192,
  TemporaryYCoordinate = 193,
  TemporaryColourTemperature = 194,
  TemporaryPrimaryNDimLevel0 = 195,
  TemporaryPrimaryNDimLevel1 = 196,
  TemporaryPrimaryNDimLevel2 = 197,
  TemporaryPrimaryNDimLevel3 = 198,
  TemporaryPrimaryNDimLevel4 = 199,
  TemporaryPrimaryNDimLevel5 = 200,
  TemporaryRedDimLevel = 201,
  TemporaryGreenDimLevel = 202,
  TemporaryBlueDimLevel = 203,
  TemporaryWhiteDimLevel = 204,
  TemporaryAmberDimLevel = 205,
  TemporaryFreecolourDimLevel = 206,
  TemporaryRgbwafControl = 207,
  TemporaryColourType = 208,
  ReportXCoordinate = 224,
  ReportYCoordinate = 225,
  ReportColourTemperatureTc = 226,
  ReportPrimaryNDimLevel0 = 227,
  ReportPrimaryNDimLevel1 = 228,
  ReportPrimaryNDimLevel2 = 229,
  ReportPrimaryNDimLevel3 = 230,
  ReportPrimaryNDimLevel4 = 231,
  ReportPrimaryNDimLevel5 = 232,
  ReportRedDimLevel = 233,
  ReportGreenDimLevel = 234,
  ReportBlueDimLevel = 235,
  ReportWhiteDimLevel = 236,
  ReportAmberDimLevel = 237,
  ReportFreecolourDimLevel = 238,
  ReportRgbwafControl = 239,
  ReportColourType = 240,
};


/**
 * @brief Selector for @ref DALIInterface::set_tc_limit.
 *
 * Identifies which of the four colour-temperature limits to write into
 * the gear via STORE COLOUR TEMPERATURE LIMIT (IEC 62386-209).
 */
enum TemperatureLimit
{
  Tc_Coolest          = 0,  /**< Warmest TC the user can request (lowest mirek). */
  Tc_Warmest          = 1,  /**< Coolest TC the user can request (highest mirek). */
  Tc_Physical_Coolest = 2,  /**< Physical lower bound the gear supports. */
  Tc_Physical_Warmest = 3   /**< Physical upper bound the gear supports. */
};


/**
 * @brief 7-bit DALI broadcast-all address.
 *
 * Use as the @c address argument to any @ref DALIInterface command that
 * should reach every gear on the bus. The DALI 7-bit address layout is:
 *
 *  - `0x00`-`0x3F`: short addresses 0..63
 *  - `0x40`-`0x4F`: group addresses 0..15
 *  - `0x7E`:        broadcast unaddressed
 *  - `0x7F`:        broadcast all (this constant)
 */
constexpr uint8_t BROADCAST = 0x7F;


/**
 * @brief Driver for a single DALI-2 bus.
 *
 * Owns one TX/RX pin pair, the hardware timer and ISRs that implement the
 * bit-level Manchester protocol, and a high-level API of DALI commands.
 * Instantiated from YAML as a `dali:` component; only one instance per
 * node is supported (the timer ISR dispatches through a static pointer).
 *
 * ### Addressing
 *
 * The 7-bit @c address argument accepted by most methods follows the
 * standard DALI encoding:
 *
 *   - `0x00`-`0x3F`: short addresses 0..63
 *   - `0x40`-`0x4F`: group addresses 0..15
 *   - `0x7E`:        broadcast unaddressed
 *   - `0x7F`:        broadcast all (see @ref BROADCAST)
 *
 * All commands take this same 7-bit value; the methods build the correct
 * 16-bit frame internally (selector bit, group/broadcast bits, etc.).
 *
 * ### Blocking & timing
 *
 * Every public command method blocks until the frame has been fully
 * transmitted (~17 ms) and the DALI-mandated inter-frame settling time
 * (~9 ms) has elapsed. Query variants additionally wait for the slave's
 * backward-frame window (~17 ms). Expect ~26 ms blocking for a simple
 * command and ~45 ms for a query.
 *
 * ### Configuration commands (sent twice)
 *
 * IEC 62386-102 §11.5 requires "configuration" commands - anything that
 * changes persistent gear state - to be transmitted twice within 100 ms.
 * The helpers in this class do that automatically; if you use the raw
 * @ref send_forward_frame overload, set @c repeat=true explicitly.
 */
class DALIInterface: public Component {
 public:
  /**
   * @brief Initialise the timer, ISRs and GPIOs.
   *
   * Called once by ESPHome at boot.
   */
  void setup() override;

  /**
   * @brief Log the current pin assignment.
   */
  void dump_config() override;

  /**
   * @brief Setup priority for ESPHome's component graph.
   *
   * @return Always @ref esphome::setup_priority::BUS so the bus is
   *         fully up before any @ref DALILight tries to use it.
   */
  float get_setup_priority() const override { return setup_priority::BUS; }

  /**
   * @brief Run DALI address discovery / commissioning.
   *
   * @note Currently a stub - logs a warning when invoked.
   */
  void search();

  /**
   * @brief Configure the GPIO used for the open-collector TX driver.
   *
   * @param tx_pin Idle-high, actively driven low to assert the bus.
   *        Set @c inverted on the pin itself in YAML if your hardware
   *        already inverts the signal.
   */
  void set_tx_pin(InternalGPIOPin *tx_pin);

  /**
   * @brief Configure the GPIO used to read the bus.
   *
   * @param rx_pin Optional - omit to disable all query commands.
   */
  void set_rx_pin(InternalGPIOPin *rx_pin);

  /**
   * @brief Transmit a 16-bit forward frame, fire-and-forget.
   *
   * @param forward_frame Assembled 16-bit forward frame (address byte
   *        in the upper 8 bits, data byte in the lower 8). Higher-level
   *        helpers in this class are usually a better choice.
   * @param repeat If @c true, transmit the same frame twice with the
   *        DALI-mandated >=22 TE settling time between attempts.
   *        Required for configuration commands (IEC 62386-102 §11.5).
   */
  void send_forward_frame(uint16_t forward_frame, bool repeat = false);

  /**
   * @brief Transmit a 16-bit forward frame and wait for the slave's
   *        backward-frame response.
   *
   * @param forward_frame Assembled 16-bit forward frame.
   * @param[out] response Decoded 8-bit response byte; only written when
   *        the function returns @c true.
   * @return @c true if a valid backward frame was received and decoded,
   *         @c false on timeout, malformed response, or when no RX pin
   *         has been configured.
   */
  bool send_forward_frame(uint16_t forward_frame, uint8_t &response);

  /**
   * @brief Direct Arc Power Control: set the gear's immediate light level.
   *
   * The gear fades from its current level to @p power using its
   * configured fade time (see @ref set_fade_time).
   *
   * @param address 7-bit DALI address.
   * @param power Target DAPC level: 0 = off, 0xFE = full brightness.
   *        0xFF is reserved as MASK ("no change") and must be avoided -
   *        the caller is responsible for clamping.
   */
  void send_dapc(uint8_t address, uint8_t power);

  /**
   * @brief Extinguish the lamp immediately, bypassing the fade time.
   *
   * @param address 7-bit DALI address.
   */
  void off(uint8_t address);

  /**
   * @brief Step the target level up using the gear's configured fade rate.
   *
   * @param address 7-bit DALI address.
   * @see set_fade_rate
   */
  void up(uint8_t address);

  /**
   * @brief Step the target level down using the gear's configured fade rate.
   *
   * @param address 7-bit DALI address.
   * @see set_fade_rate
   */
  void down(uint8_t address);

  /**
   * @brief Recall a previously-stored scene.
   *
   * @param address 7-bit DALI address.
   * @param scene Scene index, 0..15.
   * @see set_scene
   */
  void goto_scene(uint8_t address, uint8_t scene);

  /**
   * @brief Configure the gear's maximum output level.
   * @param address 7-bit DALI address.
   * @param level Maximum DAPC level (1..254).
   * @note Configuration command - transmitted twice.
   */
  void set_max_level(uint8_t address, uint8_t level);

  /**
   * @brief Configure the gear's minimum output level.
   * @param address 7-bit DALI address.
   * @param level Minimum DAPC level (1..254).
   * @note Configuration command - transmitted twice.
   */
  void set_min_level(uint8_t address, uint8_t level);

  /**
   * @brief Configure the level the gear restores after a power-on reset.
   * @param address 7-bit DALI address.
   * @param level Power-on level, 0..254, or 0xFF (MASK = restore to the
   *        last actual level before power-down).
   * @note Configuration command - transmitted twice.
   */
  void set_on_power_level(uint8_t address, uint8_t level);

  /**
   * @brief Configure the gear's fade time (used by DAPC, scenes, etc.).
   *
   * 4-bit DALI fade-time index. The actual fade time is non-linear:
   *
   *      0  no fade (instant)
   *      1  0.707 s          8  8.000 s
   *      2  1.000 s          9  11.31 s
   *      3  1.414 s         10  16.00 s
   *      4  2.000 s         11  22.63 s
   *      5  2.828 s         12  32.00 s
   *      6  4.000 s         13  45.25 s
   *      7  5.657 s (def.)  14  64.00 s
   *                         15  90.51 s
   *
   * @param address 7-bit DALI address.
   * @param fade_time Fade-time index, 0..15.
   * @note Configuration command - transmitted twice.
   */
  void set_fade_time(uint8_t address, uint8_t fade_time);

  /**
   * @brief Configure the gear's fade rate (used by UP / DOWN).
   *
   * Steps-per-second when UP / DOWN are held active:
   *
   *      1  357.8 steps/s      9  22.4 steps/s
   *      2  253.0 steps/s     10  15.8 steps/s
   *      3  178.9 steps/s     11  11.2 steps/s
   *      4  126.5 steps/s     12   7.9 steps/s
   *      5   89.4 steps/s     13   5.6 steps/s
   *      6   63.2 steps/s     14   4.0 steps/s
   *      7   44.7 steps/s     15   2.8 steps/s
   *      8   31.6 steps/s
   *
   * @param address 7-bit DALI address.
   * @param fade_rate Fade-rate index, 1..15.
   * @note Configuration command - transmitted twice.
   */
  void set_fade_rate(uint8_t address, uint8_t fade_rate);

  /**
   * @brief Configure the extended fade time.
   *
   * Allows fades shorter than @ref set_fade_time can express. Encoded
   * per IEC 62386-102 §9.5.7 as `(multiplier << 4) | base`.
   *
   * @param address 7-bit DALI address.
   * @param fade_time Encoded extended-fade-time byte.
   */
  void set_extended_fade_time(uint8_t address, uint8_t fade_time);

  /**
   * @brief Store a level into one of the gear's scenes.
   * @param address 7-bit DALI address.
   * @param scene Scene index, 0..15.
   * @param level DAPC level associated with the scene, 0..254.
   * @note Configuration command - transmitted twice.
   */
  void set_scene(uint8_t address, uint8_t scene, uint8_t level);

  /**
   * @brief Remove the gear from a previously-stored scene.
   * @param address 7-bit DALI address.
   * @param scene Scene index, 0..15.
   * @note Configuration command - transmitted twice.
   */
  void remove_from_scene(uint8_t address, uint8_t scene);

  /**
   * @brief Add the gear to a DALI group.
   * @param address 7-bit DALI address (use a short address - sending
   *        this to a group/broadcast target generally only makes sense
   *        when batch-clearing groups).
   * @param group Group index, 0..15.
   * @note Configuration command - transmitted twice.
   */
  void add_to_group(uint8_t address, uint8_t group);

  /**
   * @brief Remove the gear from a DALI group.
   * @param address 7-bit DALI address.
   * @param group Group index, 0..15.
   * @note Configuration command - transmitted twice.
   */
  void remove_from_group(uint8_t address, uint8_t group);

  /**
   * @brief Reassign a gear's short address.
   *
   * Encodes the new address per IEC 62386-102 §11.10.46: DTR0 receives
   * @c 0AAAAAA1 for a valid short address, or @c 0xFF to delete.
   *
   * @param address Current 7-bit DALI address used to reach the gear.
   * @param new_address New 6-bit short address 0..63, or 0xFF to clear.
   * @note Configuration command - transmitted twice.
   */
  void set_short_address(uint8_t address, uint8_t new_address);

  /**
   * @brief Unlock the gear's memory banks for the very next WRITE MEMORY
   *        frame.
   *
   * @param address 7-bit DALI address.
   */
  void enable_write_memory(uint8_t address);

  /**
   * @brief Write one byte into a memory-bank location (no reply).
   *
   * Typically preceded by @ref enable_write_memory.
   *
   * @param location Byte offset within the bank.
   * @param bank Memory bank number.
   * @param value Byte to write.
   * @note Configuration command - transmitted twice.
   */
  void write_memory(uint8_t location, uint8_t bank, uint8_t value);

  /**
   * @brief Read one byte from a memory-bank location.
   *
   * @param address 7-bit DALI address.
   * @param location Byte offset within the bank.
   * @param bank Memory bank number.
   * @note The result is currently only logged. TODO: return the value.
   */
  void read_memory(uint8_t address, uint8_t location, uint8_t bank);

  /**
   * @brief DT8: set the temporary CIE x chromaticity coordinate.
   *
   * Caller does not need to invoke @ref set_device_type beforehand -
   * this method enables device type 8 for the next command itself.
   *
   * @param address 7-bit DALI address.
   * @param coord 16-bit x coordinate in DALI's fixed-point format.
   */
  void send_xcoord(uint8_t address, unsigned int coord);

  /**
   * @brief DT8: set the temporary CIE y chromaticity coordinate.
   * @param address 7-bit DALI address.
   * @param coord 16-bit y coordinate in DALI's fixed-point format.
   */
  void send_ycoord(uint8_t address, unsigned int coord);

  /**
   * @brief DT8: set the temporary colour temperature and activate it.
   *
   * Converts kelvin to mired internally. Values outside 1000..20000 K
   * are rejected with a log message.
   *
   * @param address 7-bit DALI address.
   * @param kelvin Target colour temperature in K.
   */
  void set_cct(uint8_t address, unsigned int kelvin);

  /**
   * @brief DT8: set temporary RGB dim levels and activate them.
   *
   * Loads @p red, @p green and @p blue into DTR0..2, issues
   * SET TEMPORARY RGB DIM LEVEL, then ACTIVATE. Levels are 0..254
   * (IEC 62386-209 arc-power scale).
   *
   * @param address 7-bit DALI address.
   */
  void set_rgb(uint8_t address, uint8_t red, uint8_t green, uint8_t blue);

  /**
   * @brief DT8: store a colour-temperature limit on the gear.
   *
   * @param address 7-bit DALI address.
   * @param what Which of the four limits to write.
   * @param limit Colour temperature in K. Must be > 0; values outside
   *        1000..20000 K are rejected.
   * @note Configuration command - transmitted twice.
   */
  void set_tc_limit(uint8_t address, TemperatureLimit what, unsigned int limit);

  /**
   * @brief DT8: query one of the gear's colour values.
   *
   * @param address 7-bit DALI address.
   * @param query Which value to retrieve (see @ref QueryColourValue).
   * @return 16-bit colour value, or 0 on failure / missing RX pin.
   */
  uint16_t query_colour_value(uint8_t address, QueryColourValue query);

  /**
   * @brief Enable a non-default device type for the very next forward frame.
   *
   * @param device_type DALI device type number (e.g. 8 for DT8 colour).
   * @note ENABLE DEVICE TYPE applies only to the immediately following
   *       command, so it must be re-sent before every DTx frame.
   */
  void set_device_type(uint8_t device_type);

 protected:

  /**
   * @brief Per-bus state.
   *
   * Owned here, referenced from the ISRs via the static @c gbl_driver
   * pointer initialised in @ref setup.
   */
  DALIState driver_;

  /**
   * @brief Manchester-encode a 16-bit forward-frame payload.
   *
   * Each data bit (MSB first) maps to a bi-phase pair in the output:
   *
   *     logical 0  -> 0b10  (high half then low half)
   *     logical 1  -> 0b01  (low half then high half)
   *
   * @param forward_frame 16-bit forward frame to encode.
   * @return 32-bit Manchester-coded representation, MSB-first.
   */
  static uint32_t DALI_Encode(uint16_t forward_frame);

  /**
   * @brief Decode an 8-bit DALI backward frame from captured bi-phase pairs.
   *
   * The pairs are packed LSB-first in @p raw_data: bits[1:0] correspond
   * to the LAST received pair (data bit 0) and bits[15:14] to the FIRST
   * (data bit 7). The decoded byte has data bit 0 in the LSB.
   *
   * @param raw_data Raw captured bi-phase pairs.
   * @param[out] backward_frame_out Decoded 8-bit response byte.
   * @return @c true if all pairs are valid Manchester encodings.
   */
  static bool DALI_Decode(uint32_t raw_data, uint8_t *backward_frame_out);

  /**
   * @brief Internal: perform one DALI forward (and optional backward) transfer.
   *
   * Arms the timer, hands control to the TX state machine driven by the
   * timer ISR, optionally opens the slave's backward-frame window, and
   * blocks until the bus has been released and the post-frame settling
   * time has elapsed (~26..45 ms total).
   *
   * @param forward_frame 16-bit forward frame to transmit.
   * @param expect_response If @c true, open the backward-frame window
   *        after the forward frame and decode the slave's response.
   * @param[out] response Decoded 8-bit response (only written on a
   *        successful query). May be @c nullptr if the caller doesn't
   *        care about the value.
   * @return @c true on success, @c false on timeout, decode error, or
   *         missing RX pin when @p expect_response is set.
   */
  bool do_transfer_(uint16_t forward_frame, bool expect_response, uint8_t *response);

};


}  // namespace DALI
}  // namespace esphome

#endif

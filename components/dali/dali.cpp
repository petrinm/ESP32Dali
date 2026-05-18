#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "dali.h"
#include "dali_cmds.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace DALI {

static const char *TAG = "dali_interface";

// ============================================================================
// Bi-phase / Manchester pair encoding
// ============================================================================

#define BI_PHASE_HIGH           0b01
#define BI_PHASE_LOW            0b10
#define BI_PHASE_MASK           0b11

// ============================================================================
// DALI bit timing. One DALI bit is 833.33 us (1200 baud) = 2 TE.
// ============================================================================

// Half-bit time in microseconds (TE).
#define TE            (417)

// Optional offsets to compensate for asymmetric rise / fall delay of the
// physical layer driver. Zero by default.
#define TE_HIGH       (TE + 0)
#define TE_LOW        (TE - 0)

#define DALI_STOP_BIT_TIME          (4*TE)
// Minimum settling time between consecutive forward frames (~9.17 ms).
#define MIN_FORWARD_FRAME_DELAY     (22*TE)
// Required delay between forward frame and backward frame (>= 7 TE).
#define BACKWARD_FRAME_DELAY        (12*TE)

#if 0 // Strict receive timing per the DALI specification.
  #define MIN_TE      (TE - 42)
  #define MAX_TE      (TE + 42)
  #define MIN_2TE     (2*TE - 83)
  #define MAX_2TE     (2*TE + 83)
#else // Relaxed receive timing, used in practice for noisy buses.
  #define MIN_TE      (300)
  #define MAX_TE      (550)
  #define MIN_2TE     (2*TE - (2*(TE/5)))
  #define MAX_2TE     (2*TE + (2*(TE/5)))
#endif

// Backward frame length, in half-bits: start bit (2 half-bits) + 8 data
// bits * 2 half-bits = 18 half-bits. (The forward frame is 32 half-bits +
// start bit.)
#define BACKWARD_FRAME_BIT_LENGTH     18


/**
 * @brief Internal state-machine states driven by the timer / GPIO ISRs.
 *
 * Roughly mirrors the DALI frame timing diagram: the TX states walk
 * through the forward frame's half-bits, the SETTLING states wait out
 * the inter-frame gap, and the WAITING / RECEIVING states sample the
 * slave's backward frame.
 */
enum stateTag
{
  MS_IDLE = 0,                        // Bus idle, no transfer in progress.
  MS_TX_SECOND_HALF_START_BIT,        // First (low) half-bit of start bit driven; about to drive the second (high) half.
  MS_TX_DALI_FORWARD_FRAME,           // Shifting out the 32 Manchester half-bits of the forward frame.
  MS_TX_STOP_BITS,                    // Forward frame complete, driving the 4 TE stop-bit idle period.
  MS_SETTLING_BEFORE_BACKWARD,        // Stop bits done; waiting before opening the slave's response window.
  MS_SETTLING_BEFORE_IDLE,            // Frame fully done; waiting out the post-frame settling time before MS_IDLE.
  MS_WAITING_FOR_SLAVE_START_WINDOW,  // Inside the 7 TE "too early" guard window for the slave's first edge.
  MS_WAITING_FOR_SLAVE_START,         // Inside the 23 TE window during which the slave is allowed to start.
  MS_RECEIVING_ANSWER                 // Slave is transmitting; sampling its bi-phase edges.
};


/**
 * @brief Bus-level TX / RX GPIO helpers.
 *
 * These helpers speak in terms of the DALI *bus* level (idle-high,
 * open-collector). They do NOT do any inversion themselves - the
 * physical TX/RX polarity is controlled per-pin from YAML via the
 * standard ESPHome `inverted:` sub-option:
 *
 *   tx_pin:
 *     number: 17
 *     inverted: true    # if driving the bus through an NPN/opto that
 *                       # pulls bus low when the GPIO is high.
 *   rx_pin:
 *     number: 16
 *     inverted: true    # if the RX optocoupler inverts the bus state.
 *
 * In code we always use bus-level semantics:
 *   digital_write(true)  -> bus high (idle, released)
 *   digital_write(false) -> bus low  (active, pulled down)
 *   digital_read()       -> true if bus is high, false if bus is low
 *
 * The pin abstraction takes care of the actual GPIO polarity. So toggle
 * `inverted:` in YAML rather than touching these functions.
 *
 * All three helpers are called from ISR and non-ISR context, so they
 * always use the ISR-safe pin and live in IRAM.
 */

/** @brief Release the DALI bus (drive it to the idle-high state). */
static IRAM_ATTR inline void DALI_SetOutputHigh(DALIState *driver) {
  driver->tx_isr_pin.digital_write(true);
}

/** @brief Pull the DALI bus low (active state, asserting a half-bit). */
static IRAM_ATTR inline void DALI_SetOutputLow(DALIState *driver) {
  driver->tx_isr_pin.digital_write(false);
}

/** @brief Sample the current DALI bus level. @return true if bus is high. */
static IRAM_ATTR inline bool DALI_GetInput(DALIState *driver) {
  return driver->rx_isr_pin.digital_read();
}

/**
 * @brief Global driver pointer used by the timer ISR.
 *
 * The Arduino-ESP32 timer ISR signature takes no argument, so we route
 * dispatch through this static pointer. Initialised in
 * @ref DALIInterface::setup; only one instance is supported.
 */
DALIState *gbl_driver;

/**
 * IRAM_ATTR is intentionally only on the definition below; placing it on
 * both the forward declaration and the definition makes GCC complain
 * about conflicting section attributes (.iram1.N counters).
 */
static void gpio_interrupt(DALIState *driver);

/**
 * @brief Hardware-timer ISR driving the TX state machine and RX timeouts.
 *
 * Fires on every armed timer alarm. The current
 * @ref DALIState::state value selects what work is done this tick:
 *
 *  - During TX (`MS_TX_*`), each tick clocks out one Manchester half-bit
 *    of the forward frame (one full bit takes two timer alarms).
 *  - During the settling and slave-window states (`MS_SETTLING_*`,
 *    `MS_WAITING_*`), the alarm acts as a timeout: if it fires while
 *    we're still waiting for the slave, we return to MS_IDLE.
 *
 * Runs entirely in IRAM (HOT + IRAM_ATTR) so it can execute while the
 * flash cache is busy.
 */
static void IRAM_ATTR HOT timer_interrupt()
{
  DALIState *driver = gbl_driver;

  switch(driver->state)
  {
    case MS_TX_SECOND_HALF_START_BIT:
      // Drive the second (high) half of the start bit.
      DALI_SetOutputHigh(driver);
      driver->bitcount = 0;

      // Timer auto-reloads at TE for the first data half-bit.
      driver->state = MS_TX_DALI_FORWARD_FRAME;

      break;

    case MS_TX_DALI_FORWARD_FRAME:
      // Shift out the next Manchester half-bit (MSB first).
      if (driver->forward_frame & 0x80000000)
        DALI_SetOutputHigh(driver);
      else
        DALI_SetOutputLow(driver);

      driver->forward_frame <<= 1;
      driver->bitcount++;

      // All 32 half-bits emitted -> move on to the stop-bit idle period.
      if (driver->bitcount == 32)
        driver->state = MS_TX_STOP_BITS;

      // Timer auto-reloads at TE for the next half-bit.
      break;

    case MS_TX_STOP_BITS:
      // Release the bus to high for the stop-bit idle period.
      DALI_SetOutputHigh(driver);

      // The first half of the first stop bit has just been output.
      if (driver->wait_for_answer)
      {
        // Wait out the remainder of the 4 TE stop-bit window before
        // opening the slave's response window.
        timerAlarm(driver->timer, 4 * TE, false, 0);

        // Reset RX state - bitcount was used by the TX path.
        driver->backward_frame = 0;
        driver->bitcount = 0;
        driver->state = MS_SETTLING_BEFORE_BACKWARD;
      }
      else
      {
        // No slave response expected - just wait out the inter-frame
        // idle time. Add 3 TE of margin on top of the spec minimum so
        // we're not right at the edge of the timing tolerance.
        timerAlarm(driver->timer, (4 + 22 + 3) * TE, false, 0);
        driver->state = MS_SETTLING_BEFORE_IDLE;
      }
      break;

    case MS_SETTLING_BEFORE_BACKWARD:
      // 4 TE after the last forward-frame half-bit - bus is idle, now
      // open the "too-early" guard window for the slave's first edge.
      DALI_SetOutputHigh(driver);

      // The slave must NOT answer in the next 7 TE.
      timerAlarm(driver->timer, 7 * MIN_TE, false, 0);

      // The RX ISR is permanently attached (see setup()); state gating
      // means it ignores edges in all states except MS_WAITING_FOR_*
      // and MS_RECEIVING_ANSWER. No attach_interrupt() call here -
      // that's not ISR-safe.

      driver->state = MS_WAITING_FOR_SLAVE_START_WINDOW;
      break;

    case MS_WAITING_FOR_SLAVE_START_WINDOW:
      // Guard window elapsed without an early edge - now arm the proper
      // 23 TE window during which the slave is allowed to start
      // transmitting. Next event we expect is a GPIO interrupt.
      timerAlarm(driver->timer, 23 * MAX_TE, false, 0);
      driver->state = MS_WAITING_FOR_SLAVE_START;
      break;

    case MS_WAITING_FOR_SLAVE_START:
      // 23 TE window expired with no slave edge -> receive timeout.
      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    case MS_RECEIVING_ANSWER:
      // Receive timeout in the middle of a frame. Idle the bus before
      // the next forward frame: we don't track the last edge of the
      // received frame, so conservatively wait 23 TE (>= the 22 TE
      // specification minimum).
      timerAlarm(driver->timer, 23 * MAX_TE, false, 0);
      DALI_SetOutputHigh(driver);
      driver->state = MS_SETTLING_BEFORE_IDLE;
      break;

    case MS_SETTLING_BEFORE_IDLE:
      // Post-frame settling complete - bus is idle / available again.
      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    default:
      // Unexpected timer alarm in idle / TX states - force a clean reset.
      DALI_SetOutputHigh(driver);
      timerStop(driver->timer);
      driver->state = MS_IDLE;
  }
}

/**
 * @brief GPIO edge ISR for the RX pin, sampling the slave's backward frame.
 *
 * Attached once during @ref DALIInterface::setup with
 * @c INTERRUPT_ANY_EDGE. The handler is state-gated: edges outside the
 * `MS_WAITING_FOR_*` and `MS_RECEIVING_ANSWER` states are ignored, so we
 * never have to attach/detach the interrupt around individual transfers
 * (which is not ISR-safe anyway).
 *
 * Each edge is timestamped using the hardware timer; the resulting pulse
 * width determines whether we just received one TE (single half-bit) or
 * two TE (a bit transition that "swallows" two half-bits of the same
 * level), and the level at the edge picks which Manchester pair we add
 * to @ref DALIState::backward_frame.
 */
static void IRAM_ATTR HOT gpio_interrupt(DALIState *driver)
{
  switch (driver->state) {
    case MS_WAITING_FOR_SLAVE_START_WINDOW:
      // Slave answered too early - inside the 7 TE guard window.
      // Abort and let the host wait-loop see MS_IDLE.
      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    case MS_WAITING_FOR_SLAVE_START: {
      // First edge of the backward frame's start bit. Slave is now
      // actively transmitting.

      // Arm the overall receive timeout (full frame must fit in ~22 TE).
      timerAlarm(driver->timer, 22 * MAX_TE, false, 0);

      // Reset the timer count so the next edge gives us a pulse width.
      timerWrite(driver->timer, 0);

      driver->backward_frame <<= 1;

      bool dali_bus_level = DALI_GetInput(driver);
      if (dali_bus_level)
        driver->backward_frame |= BI_PHASE_HIGH;

      driver->bitcount += 1;
      driver->state = MS_RECEIVING_ANSWER;
      break;
    }
    case MS_RECEIVING_ANSWER: {
      // Subsequent edge inside the backward frame - classify by width.
      bool dali_bus_level = DALI_GetInput(driver);
      uint32_t pulse_us = timerRead(driver->timer);
      timerWrite(driver->timer, 0);

      if (MIN_2TE < pulse_us && pulse_us < MAX_2TE)
      {
        // 2 TE pulse: spans two half-bits at the same level.
        driver->backward_frame <<= 2;

        if (dali_bus_level)
          driver->backward_frame |= BI_PHASE_HIGH;
        else
          driver->backward_frame |= BI_PHASE_LOW;

        driver->bitcount += 2;
      }
      else if (MIN_TE < pulse_us && pulse_us < MAX_TE)
      {
        // 1 TE pulse: single half-bit at the new level.
        driver->backward_frame <<= 1;

        if (dali_bus_level)
          driver->backward_frame |= BI_PHASE_HIGH;

        driver->bitcount += 1;
      }
      else
      {
        // Pulse is not a valid DALI bi-phase width. Abort the receive
        // and let the host wait-loop see MS_IDLE.
        timerStop(driver->timer);
        driver->bitcount = 0;
        driver->backward_frame = 0;
        driver->state = MS_IDLE;
        break;
      }

      if (driver->bitcount == BACKWARD_FRAME_BIT_LENGTH)
      {
        // Frame complete - wait for the post-frame idle time before
        // returning to MS_IDLE via MS_SETTLING_BEFORE_IDLE.
        timerWrite(driver->timer, 0);
        timerAlarm(driver->timer, 22 * MAX_TE, false, 0);
        driver->state = MS_SETTLING_BEFORE_IDLE;
      }

      break;
    }

    default:
      // Spurious edge in idle / TX states - ignored.
      break;
  }
}

void DALIInterface::setup()
{
  int div = getApbFrequency() / 1000000;
  ESP_LOGD(TAG, "DALI setup %d", div);

  if (gbl_driver != nullptr) {
    ESP_LOGE(TAG, "Only one DALIInterface instance is supported");
    this->mark_failed();
    return;
  }
  DALIState *driver = &driver_;
  gbl_driver = driver;

  // Make sure the state machine starts in a known state. (DALIState is
  // value-initialised when ESPHome allocates DALIInterface, but be
  // explicit so this also survives any future allocation change.)
  driver->state = MS_IDLE;
  driver->bitcount = 0;
  driver->forward_frame = 0;
  driver->backward_frame = 0;
  driver->wait_for_answer = false;

  // Configure pins and capture their ISR-safe handles. The handles are
  // used by the timer/gpio ISRs and so must be populated before any ISR
  // can fire.
  if (driver->tx_pin == nullptr) {
    ESP_LOGE(TAG, "tx_pin not configured");
    this->mark_failed();
    return;
  }
  driver->tx_pin->setup();
  driver->tx_isr_pin = driver->tx_pin->to_isr();

  if (driver->rx_pin != nullptr) {
    driver->rx_pin->setup();
    driver->rx_isr_pin = driver->rx_pin->to_isr();
  }

  // Force the DALI bus high (release the open-collector driver).
  DALI_SetOutputHigh(driver);

  // Setup timer. 1 MHz -> 1 count = 1 µs. New Arduino-ESP32 timer API
  // (since core 3.0) creates and starts the timer in timerBegin(); we
  // immediately stop it because no alarm is armed yet anyway, and we
  // want it stopped while idle.
  driver->timer = timerBegin(1000000);
  timerAttachInterrupt(driver->timer, &timer_interrupt);
  timerStop(driver->timer);

  // Attach the RX edge interrupt once. The state machine's default case
  // ignores edges in idle/TX states, so we don't need to attach/detach
  // it around individual transfers (which is not ISR-safe anyway).
  if (driver->rx_pin != nullptr) {
    driver->rx_pin->attach_interrupt(&gpio_interrupt, driver, gpio::INTERRUPT_ANY_EDGE);
  } else {
    ESP_LOGW(TAG, "rx_pin not configured - query commands will time out");
  }
}

void DALIInterface::set_tx_pin(InternalGPIOPin *tx_pin)
{
  driver_.tx_pin = tx_pin;
}

void DALIInterface::set_rx_pin(InternalGPIOPin *rx_pin)
{
  driver_.rx_pin = rx_pin;
}

void DALIInterface::dump_config()
{
  ESP_LOGCONFIG(TAG, "DALIInterface");
  LOG_PIN("  tx_pin: ", this->driver_.tx_pin);
  LOG_PIN("  rx_pin: ", this->driver_.rx_pin);
}

/**
 * @brief Manchester-encode a 16-bit forward-frame payload.
 *
 * See the matching declaration in `dali.h` for the bi-phase mapping.
 */
uint32_t DALIInterface::DALI_Encode(uint16_t forward_frame)
{
  uint32_t coded = 0;
  for (int i = 15; i >= 0; i--) // MSB to LSB
  {
    if (forward_frame & (1 << i))
    {
      coded <<= 2;
      coded |= 0b01;
    }
    else
    {
      coded <<= 2;
      coded |= 0b10;
    }
  }
  return coded;
}

bool DALIInterface::DALI_Decode(uint32_t raw_data, uint8_t *backward_frame_out)
{
  uint8_t backward_frame = 0;
  for (int i = 0; i < 8; i++)
  {
    switch (raw_data & BI_PHASE_MASK)
    {
    case BI_PHASE_HIGH:
      backward_frame |= (1u << i);
      break;
    case BI_PHASE_LOW:
      break;
    default:
      return false;
    }
    raw_data >>= 2;
  }

  *backward_frame_out = backward_frame;
  return true;
}

void DALIInterface::send_special_command(uint8_t opcode, uint8_t data, bool send_twice)
{
  send_forward_frame((static_cast<uint16_t>(opcode) << 8) | data, send_twice);
}

bool DALIInterface::send_special_query(uint8_t opcode, uint8_t data)
{
  uint8_t response;
  if (!send_forward_frame((static_cast<uint16_t>(opcode) << 8) | data, response)) {
    return false;
  }
  return response == 0xFF;
}

void DALIInterface::commissioning_terminate() {
  send_special_command(DALI_CMD_TERMINATE);
}

void DALIInterface::commissioning_initialise_unaddressed()
{
  send_special_command(DALI_CMD_INITIALISE, 0xFF, true);
}

void DALIInterface::commissioning_initialise_all() {
  send_special_command(DALI_CMD_INITIALISE, 0x00, true);
}

void DALIInterface::commissioning_randomise() {
  send_special_command(DALI_CMD_RANDOMIZE, 0, true);
}

void DALIInterface::commissioning_set_search_address(uint32_t addr24)
{
  send_special_command(DALI_CMD_SEARCH_ADDRH, (addr24 >> 16) & 0xFF);
  send_special_command(DALI_CMD_SEARCH_ADDRM, (addr24 >> 8) & 0xFF);
  send_special_command(DALI_CMD_SEARCH_ADDRL, addr24 & 0xFF);
}

bool DALIInterface::commissioning_compare() {
  return send_special_query(DALI_CMD_COMPARE);
}

void DALIInterface::commissioning_withdraw() {
  send_special_command(DALI_CMD_WITHDRAW);
}

void DALIInterface::commissioning_program_short_address(uint8_t short_address)
{
  if (short_address > 63) {
    ESP_LOGW(TAG, "commissioning_program_short_address: invalid address %u", short_address);
    return;
  }
  send_special_command(DALI_CMD_PROGRAM_SHORT_ADDRESS, static_cast<uint8_t>((short_address << 1) | 0x01), true);
}

bool DALIInterface::find_next_random_address_(uint32_t low, uint32_t high, uint32_t *found)
{
  if (found == nullptr || low > high) {
    return false;
  }

  if (low == high) {
    commissioning_set_search_address(low);
    if (!commissioning_compare()) {
      return false;
    }
    *found = low;
    return true;
  }

  commissioning_set_search_address(high);
  if (!commissioning_compare()) {
    return false;
  }

  const uint32_t mid = low + (high - low) / 2;
  if (find_next_random_address_(low, mid, found)) {
    return true;
  }
  return find_next_random_address_(mid + 1, high, found);
}

size_t DALIInterface::discover_random_addresses_(std::vector<uint32_t> &addresses, bool broadcast_initialise,
                                                 bool withdraw_each)
{
  addresses.clear();

  if (driver_.rx_pin == nullptr) {
    ESP_LOGE(TAG, "discover_random_addresses_: rx_pin is required");
    return 0;
  }

  commissioning_terminate();
  if (broadcast_initialise) {
    commissioning_initialise_all();
  } else {
    commissioning_initialise_unaddressed();
  }
  commissioning_randomise();
  delay(100);

  uint32_t search_from = 0;
  constexpr uint32_t k_max_random = 0xFFFFFF;

  while (search_from <= k_max_random) {
    uint32_t found = 0;
    if (!find_next_random_address_(search_from, k_max_random, &found)) {
      break;
    }

    addresses.push_back(found);

    if (withdraw_each) {
      commissioning_set_search_address(found);
      commissioning_withdraw();
    }

    if (found == k_max_random) {
      break;
    }
    search_from = found + 1;
  }

  commissioning_terminate();
  return addresses.size();
}

void DALIInterface::search()
{
  std::vector<uint32_t> addresses;
  const size_t count = discover_random_addresses_(addresses, false, true);
  ESP_LOGI(TAG, "search(): found %u unaddressed device(s)", count);
  for (uint32_t addr : addresses) {
    ESP_LOGI(TAG, "  random address 0x%06X", addr);
  }
}

void DALIInterface::identify_device(uint8_t address)
{
  ESP_LOGI(TAG, "identify_device: address=%u", address);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_IDENTIFY_DEVICE, true);
}

size_t DALIInterface::commission_unaddressed_gear(uint8_t start_short_address, uint8_t max_devices,
                                                  bool assign_addresses)
{
  if (driver_.rx_pin == nullptr) {
    ESP_LOGE(TAG, "commission_unaddressed_gear: rx_pin is required");
    return 0;
  }

  if (max_devices == 0) {
    return 0;
  }

  if (assign_addresses && start_short_address > 63) {
    ESP_LOGE(TAG, "commission_unaddressed_gear: start_short_address must be 0..63");
    return 0;
  }

  commissioning_terminate();
  commissioning_initialise_unaddressed();
  commissioning_randomise();
  delay(100);

  size_t processed = 0;
  uint32_t search_from = 0;
  uint8_t next_short = start_short_address;
  constexpr uint32_t k_max_random = 0xFFFFFF;

  while (processed < max_devices && search_from <= k_max_random) {
    uint32_t found = 0;
    if (!find_next_random_address_(search_from, k_max_random, &found)) {
      break;
    }

    commissioning_set_search_address(found);

    if (assign_addresses) {
      if (next_short > 63) {
        ESP_LOGW(TAG, "commission_unaddressed_gear: ran out of short addresses");
        commissioning_withdraw();
        break;
      }
      commissioning_program_short_address(next_short);
      ESP_LOGI(TAG, "commission: short address %u <- random 0x%06X", next_short, found);
      next_short++;
    } else {
      ESP_LOGI(TAG, "commission: random address 0x%06X (no short address assigned)", found);
    }

    commissioning_withdraw();
    processed++;

    if (found == k_max_random) {
      break;
    }
    search_from = found + 1;
  }

  commissioning_terminate();
  ESP_LOGI(TAG, "commission_unaddressed_gear: %u device(s) processed", processed);
  return processed;
}

bool DALIInterface::do_transfer_(uint16_t forward_frame, bool expect_response, uint8_t *response)
{
  DALIState *driver = &driver_;

  if (expect_response && driver->rx_pin == nullptr) {
    ESP_LOGW(TAG, "Query requested but rx_pin is not configured");
    return false;
  }

  driver->state = MS_TX_SECOND_HALF_START_BIT;
  driver->wait_for_answer = expect_response;
  driver->forward_frame = DALI_Encode(forward_frame);
  driver->backward_frame = 0;
  driver->bitcount = 0;

  // Pull down to indicate first half-bit of start bit
  DALI_SetOutputLow(driver);

  // Activate the timer to output the forward frame. The new
  // Arduino-ESP32 timerAlarm() combines what used to be
  // timerAlarmWrite() + timerAlarmEnable().
  timerWrite(driver->timer, 0);
  timerAlarm(driver->timer, TE, true, 0);
  timerStart(driver->timer);

  // Wait for the transmission (and optional reception) to complete in
  // the timer/gpio ISRs. Worst case is forward frame (~17 ms) + slave
  // window (up to ~17 ms) + settling (~9 ms) ~= 45 ms. Use 100 ms to
  // stay well clear of the edges.
  const int max_iters = 100 / 2;
  int timeout = max_iters;
  while (driver->state != MS_IDLE) {
    delay(2);
    if (--timeout == 0) {
      ESP_LOGW(TAG, "DALI transfer timed out in state %u", driver->state);
      // Force the state machine back to a clean idle state so the next
      // call doesn't start with a half-armed timer.
      timerStop(driver->timer);
      DALI_SetOutputHigh(driver);
      driver->state = MS_IDLE;
      return false;
    }
  }

  DALI_SetOutputHigh(driver);

  if (!expect_response) {
    return true;
  }

  // bitcount tracks half-bits received; a complete backward frame is
  // BACKWARD_FRAME_BIT_LENGTH (= 18). Anything less means the slave
  // didn't answer (or sent a malformed frame).
  if (driver->bitcount != BACKWARD_FRAME_BIT_LENGTH) {
    ESP_LOGD(TAG, "No backward frame (got %u half-bits)", driver->bitcount);
    return false;
  }

  uint8_t decoded;
  if (!DALI_Decode(driver->backward_frame, &decoded)) {
    ESP_LOGE(TAG, "Failed to decode backward frame: %x", driver->backward_frame);
    return false;
  }

  ESP_LOGD(TAG, "backward_frame: coded=%x, decoded=%x", driver->backward_frame, decoded);
  if (response != nullptr) {
    *response = decoded;
  }
  return true;
}

void DALIInterface::send_forward_frame(uint16_t forward_frame, bool repeat)
{
  do_transfer_(forward_frame, false, nullptr);
  if (repeat) {
    // DALI configuration commands must be sent twice within 100 ms; the
    // state machine already inserts the >=22 TE settling time between
    // transfers, so back-to-back calls are fine.
    do_transfer_(forward_frame, false, nullptr);
  }
}

bool DALIInterface::send_forward_frame(uint16_t forward_frame, uint8_t &response)
{
  return do_transfer_(forward_frame, true, &response);
}


void DALIInterface::send_dapc(uint8_t address, uint8_t power)
{
  ESP_LOGD(TAG, "send_dapc: address=%d, power=%d", address, power);
  send_forward_frame((0x7f & address) << 9 | power);
}

void DALIInterface::off(uint8_t address)
{
  ESP_LOGD(TAG, "off: address=%d", address);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_OFF);
}

void DALIInterface::up(uint8_t address)
{
  ESP_LOGD(TAG, "up: address=%d", address);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_UP);
}

void DALIInterface::down(uint8_t address)
{
  ESP_LOGD(TAG, "down: address=%d", address);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_DOWN);
}

void DALIInterface::goto_scene(uint8_t address, uint8_t scene)
{
  ESP_LOGD(TAG, "goto_scene: address=%d, scene=%d", address, scene);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | (DALI_CMD_GO_TO_SCENE + scene));
}

void DALIInterface::set_max_level(uint8_t address, uint8_t level)
{
  ESP_LOGD(TAG, "set_max_level: address=%d, level=%d", address, level);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | level);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_MAX_LEVEL, true);
}

void DALIInterface::set_min_level(uint8_t address, uint8_t level)
{
  ESP_LOGD(TAG, "set_min_level: address=%d, level=%d", address, level);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | level);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_MIN_LEVEL, true);
}

void DALIInterface::set_on_power_level(uint8_t address, uint8_t level)
{
  ESP_LOGD(TAG, "set_on_power_level: address=%d, level=%d", address, level);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | level);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_POWER_ON_LEVEL, true);
}

namespace {

/** Full 0..254 sweep duration per fade-time index, IEC 62386-102 Table 4. */
static constexpr uint32_t STANDARD_FADE_DURATION_MS[16] = {
    0,     // 0 - use extended fade time
    707,   // 1  0.707 s
    1000,  // 2  1.000 s
    1414,  // 3  1.414 s
    2000,  // 4  2.000 s
    2828,  // 5  2.828 s
    4000,  // 6  4.000 s
    5657,  // 7  5.657 s
    8000,  // 8  8.000 s
    11310, // 9  11.31 s
    16000, // 10 16.00 s
    22630, // 11 22.63 s
    32000, // 12 32.00 s
    45250, // 13 45.25 s
    64000, // 14 64.00 s
    90510, // 15 90.51 s
};

uint8_t closest_standard_fade_index(uint32_t duration_ms) {
  uint8_t best = 1;
  uint32_t best_diff = UINT32_MAX;
  for (uint8_t i = 1; i <= 15; i++) {
    const uint32_t t = STANDARD_FADE_DURATION_MS[i];
    const uint32_t diff = (t > duration_ms) ? (t - duration_ms) : (duration_ms - t);
    if (diff < best_diff) {
      best_diff = diff;
      best = i;
    }
  }
  return best;
}

/** Extended fade: T_ms = 100 * (base + 1) * 10^multiplier (multiplier 1..7). */
bool encode_extended_fade_duration_ms(uint32_t duration_ms, uint8_t *encoded_out) {
  uint32_t best_diff = UINT32_MAX;
  uint8_t best_base = 0;
  uint8_t best_mult = 1;

  for (uint8_t mult = 1; mult <= 7; mult++) {
    const uint32_t scale = (uint32_t) powf(10.0f, (float) mult);
    for (uint8_t base = 0; base <= 15; base++) {
      const uint32_t t = 100u * (uint32_t) (base + 1) * scale;
      const uint32_t diff = (t > duration_ms) ? (t - duration_ms) : (duration_ms - t);
      if (diff < best_diff) {
        best_diff = diff;
        best_base = base;
        best_mult = mult;
      }
    }
  }

  *encoded_out = (uint8_t) ((best_mult << 4) | best_base);
  return true;
}

}  // namespace

void DALIInterface::configure_fade_duration(uint8_t address, uint32_t duration_ms)
{
  if (duration_ms == 0) {
    ESP_LOGD(TAG, "configure_fade_duration: address=%d, instant", address);
    this->set_extended_fade_time(address, 0x50);
    this->set_fade_time(address, 0);
    return;
  }

  if (duration_ms <= STANDARD_FADE_DURATION_MS[15]) {
    const uint8_t index = closest_standard_fade_index(duration_ms);
    ESP_LOGD(TAG, "configure_fade_duration: address=%d, %u ms -> fade_time index %u (~%u ms)", address, duration_ms,
             index, STANDARD_FADE_DURATION_MS[index]);
    this->set_fade_time(address, index);
    return;
  }

  uint8_t encoded = 0;
  encode_extended_fade_duration_ms(duration_ms, &encoded);
  ESP_LOGD(TAG, "configure_fade_duration: address=%d, %u ms -> extended 0x%02X", address, duration_ms, encoded);
  this->set_extended_fade_time(address, encoded);
  this->set_fade_time(address, 0);
}

void DALIInterface::set_fade_time(uint8_t address, uint8_t fade_time)
{
  ESP_LOGD(TAG, "set_fade_time: address=%d, fade_time=%d", address, fade_time);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | fade_time);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_FADE_TIME, true);
}

void DALIInterface::set_fade_rate(uint8_t address, uint8_t fade_rate)
{
  ESP_LOGD(TAG, "set_fade_rate: address=%d, fade_rate=%d", address, fade_rate);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | fade_rate);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_FADE_RATE, true);
}

void DALIInterface::set_extended_fade_time(uint8_t address, uint8_t fade_time)
{
  ESP_LOGD(TAG, "set_extended_fade_time: address=%d, fade_time=%d", address, fade_time);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | fade_time);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_EXTENDED_FADE_TIME, true);
}

void DALIInterface::set_scene(uint8_t address, uint8_t scene, uint8_t level)
{
  ESP_LOGD(TAG, "set_scene: address=%d, scene=%d, level=%d", address, scene, level);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | level);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | (DALI_CMD_SET_SCENE + scene), true);
}

void DALIInterface::remove_from_scene(uint8_t address, uint8_t scene)
{
  ESP_LOGD(TAG, "remove_from_scene: address=%d, scene=%d", address, scene);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | (DALI_CMD_REMOVE_FROM_SCENE + scene), true);
}

void DALIInterface::add_to_group(uint8_t address, uint8_t group)
{
  ESP_LOGD(TAG, "add_to_group: address=%d, group=%d", address, group);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | (DALI_CMD_ADD_TO_GROUP + group), true);
}

void DALIInterface::remove_from_group(uint8_t address, uint8_t group)
{
  ESP_LOGD(TAG, "remove_from_group: address=%d, group=%d", address, group);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | (DALI_CMD_REMOVE_FROM_GROUP + group), true);
}

void DALIInterface::set_short_address(uint8_t old_address, uint8_t new_address)
{
  ESP_LOGD(TAG, "set_short_address: old_address=%d, new_address=%d", old_address, new_address);
  // IEC 62386-102 §11.10.46: DTR0 must hold (0AAAAAA1) for valid short
  // addresses, or 0xFF to delete the short address.
  uint8_t dtr0 = (new_address == 0xFF) ? 0xFF : (((new_address & 0x3F) << 1) | 0x01);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | dtr0);
  send_forward_frame((0x7f & old_address) << 9 | 0x0100 | DALI_CMD_SET_SHORT_ADDRESS, true);
}

void DALIInterface::enable_write_memory(uint8_t address)
{
  ESP_LOGD(TAG, "enable_write_memory: address=%d", address);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_ENABLE_WRITE_MEMORY);
}

void DALIInterface::write_memory(uint8_t location, uint8_t bank, uint8_t value)
{
  ESP_LOGD(TAG, "write_memory: location=%d, bank=%d, value=%d", location, bank, value);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | location);
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | bank);
  send_forward_frame((DALI_CMD_WRITE_MEMORY_LOCATION_NO_REPLY << 8) | value, true);
}

void DALIInterface::read_memory(uint8_t address, uint8_t location, uint8_t bank)
{
  ESP_LOGD(TAG, "read_memory: address=%d, location=%d, bank=%d", address, location, bank);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | location);
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | bank);
  uint8_t value;
  if (send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_READ_MEMORY_LOCATION, value)) {
    ESP_LOGD(TAG, "   value=%d", value);
  } else {
    ESP_LOGW(TAG, "   no response from gear");
  }
}

// ============================================================================
// DT8 (colour control) helpers.
//
// DT8 commands must:
//   (a) be addressed as commands (selector bit S = 1, i.e. | 0x0100), and
//   (b) be immediately preceded by ENABLE_DEVICE_TYPE 8, which only
//       affects the *next* command - so it must be repeated for every
//       DT8 frame sent.
// ============================================================================

void DALIInterface::send_xcoord(uint8_t address, unsigned int coord)
{
  ESP_LOGD(TAG, "send_xcoord: address=%d, xcoord=%d", address, coord);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | (0xFF & (coord >> 0))); // LSB
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | (0xFF & (coord >> 8))); // MSB
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_TEMPORARY_X_COORDINATE);
}

void DALIInterface::send_ycoord(uint8_t address, unsigned int coord)
{
  ESP_LOGD(TAG, "send_ycoord: address=%d, ycoord=%d", address, coord);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | (0xFF & (coord >> 0))); // LSB
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | (0xFF & (coord >> 8))); // MSB
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_TEMPORARY_Y_COORDINATE);
}

void DALIInterface::set_cct(uint8_t address, unsigned int kelvin)
{
  if (kelvin < 1000 || kelvin > 20000) {
    ESP_LOGE(TAG, "set_cct: invalid kelvin %u", kelvin);
    return;
  }

  unsigned int mirek = (1000000 / kelvin);
  ESP_LOGD(TAG, "set_cct: address=%d, kelvin=%u, mirek=%u", address, kelvin, mirek);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | (0xFF & (mirek >> 0))); // LSB
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | (0xFF & (mirek >> 8))); // MSB
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_TEMPORARY_COLOUR_TEMPERATURE);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_ACTIVATE);
}

void DALIInterface::set_rgb(uint8_t address, uint8_t red, uint8_t green, uint8_t blue)
{
  ESP_LOGD(TAG, "set_rgb: address=%d, r=%u, g=%u, b=%u", address, red, green, blue);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | red);
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | green);
  send_forward_frame((DALI_CMD_DTR2_DATA << 8) | blue);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_SET_TEMPORARY_RGB_DIM_LEVEL);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_ACTIVATE);
}

void DALIInterface::set_tc_limit(uint8_t address, TemperatureLimit what, unsigned int limit)
{
  if (limit < 1000 || limit > 20000) {
    ESP_LOGE(TAG, "set_tc_limit: invalid kelvin %u", limit);
    return;
  }

  unsigned int mirek = (1000000 / limit);
  ESP_LOGD(TAG, "set_tc_limit: address=%d, what=%d, mirek=%d", address, what, mirek);
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | (0xFF & (mirek >> 0))); // LSB
  send_forward_frame((DALI_CMD_DTR1_DATA << 8) | (0xFF & (mirek >> 8))); // MSB
  send_forward_frame((DALI_CMD_DTR2_DATA << 8) | what);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);
  send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_STORE_COLOUR_TEMPERATURE_LIMIT, true);
}

void DALIInterface::set_device_type(uint8_t device_type)
{
  ESP_LOGD(TAG, "set_device_type: device_type=%d", device_type);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | device_type);
}

uint16_t DALIInterface::query_colour_value(uint8_t address, QueryColourValue query)
{
  send_forward_frame((DALI_CMD_DTR0_DATA << 8) | query);
  send_forward_frame((DALI_CMD_ENABLE_DEVICE_TYPE << 8) | 8);

  uint8_t msb = 0, lsb = 0;
  if (!send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_QUERY_COLOUR_VALUE, msb)) {
    return 0;
  }
  if (!send_forward_frame((0x7f & address) << 9 | 0x0100 | DALI_CMD_QUERY_CONTENT_DTR0, lsb)) {
    return 0;
  }
  return ((uint16_t) msb << 8) | lsb;
}


}  // namespace DALI
}  // namespace esphome

#endif

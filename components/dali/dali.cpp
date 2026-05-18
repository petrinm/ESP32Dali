#if defined(USE_ARDUINO) && defined(USE_ESP32)

#include "dali.h"
#include "dali_cmds.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"



namespace esphome {
namespace DALI {

static const char *TAG = "dali_interface";


/*******************************************************************************
 * Typedefs and defines
 ******************************************************************************/

#define BI_PHASE_HIGH           0b01
#define BI_PHASE_LOW            0b10
#define BI_PHASE_MASK           0b11


/*******************************************************************************
 * Define DALI timing values. One Bit on a DALI bus is
 * 833.33µs (1200 bauds) which corresponds to 2TE.
 ******************************************************************************/

#define TE            (417)                     // half bit time = 417 usec

/* Transmission timing, option to compensate for unequal physical layer delay in rising or falling edge*/
#define TE_HIGH       (TE + 0)
#define TE_LOW        (TE - 0)

#define DALI_STOP_BIT_TIME          (4*TE)
#define MIN_FORWARD_FRAME_DELAY     (22*TE)     // minimum time between two forward frames - 9.17ms ==> 22 x TE time
#define BACKWARD_FRAME_DELAY        (12*TE)     // time between forward frame and backward frame >= 7TE

#if 0 /* strict receive timing according to specification  */
  #define MIN_TE      (TE - 42)                 // minimum half bit time
  #define MAX_TE      (TE + 42)                 // maximum half bit time
  #define MIN_2TE     (2*TE - 83)               // minimum full bit time
  #define MAX_2TE     (2*TE + 83)               // maximum full bit time
#else /* More relaxed receive timing */
  #define MIN_TE      (300)                     // minimum half bit time
  #define MAX_TE      (550)                     // maximum half bit time
  #define MIN_2TE     (2*TE - (2*(TE/5)))       // minimum full bit time
  #define MAX_2TE     (2*TE + (2*(TE/5)))       // maximum full bit time
#endif

// Backward frame length, in half-bits: start bit (2 half-bits) + 8 data
// bits * 2 half-bits = 18 half-bits. (The forward frame is 32 half-bits +
// start bit.)
#define BACKWARD_FRAME_BIT_LENGTH     18



/* State machine states: */
typedef enum stateTag
{
  MS_IDLE = 0,                        // bus idle
  MS_TX_SECOND_HALF_START_BIT,        //
  MS_TX_DALI_FORWARD_FRAME,           // sending the dali forward frame
  MS_TX_STOP_BITS,                    //
  MS_SETTLING_BEFORE_BACKWARD,        // settling between forward and backward - stop bits
  MS_SETTLING_BEFORE_IDLE,            // settling before going to idle, after forward frame
  MS_WAITING_FOR_SLAVE_START_WINDOW,  // waiting for 7Te, start of slave Tx window
  MS_WAITING_FOR_SLAVE_START,         // start of slave Tx window
  MS_RECEIVING_ANSWER                 // receiving slave message
} MASTER_STATE;


// These helpers speak in terms of the DALI *bus* level (idle-high,
// open-collector). They do NOT do any inversion themselves - the
// physical TX/RX polarity is controlled per-pin from YAML via the
// standard ESPHome `inverted:` sub-option:
//
//   tx_pin:
//     number: 17
//     inverted: true    # if driving the bus through an NPN/opto that
//                       # pulls bus low when the GPIO is high.
//   rx_pin:
//     number: 16
//     inverted: true    # if the RX optocoupler inverts the bus state.
//
// In code we always use bus-level semantics:
//   digital_write(true)  -> bus high (idle, released)
//   digital_write(false) -> bus low  (active, pulled down)
//   digital_read()       -> true if bus is high, false if bus is low
//
// The pin abstraction takes care of the actual GPIO polarity. So toggle
// `inverted:` in YAML rather than touching these functions.
//
// Both helpers are called from ISR and non-ISR context, so they always
// use the ISR-safe pin and live in IRAM.
static IRAM_ATTR inline void DALI_SetOutputHigh(DALIState *driver) {
  driver->tx_isr_pin.digital_write(true);
}

static IRAM_ATTR inline void DALI_SetOutputLow(DALIState *driver) {
  driver->tx_isr_pin.digital_write(false);
}

static IRAM_ATTR inline bool DALI_GetInput(DALIState *driver) {
  return driver->rx_isr_pin.digital_read();
}


/// Run timer interrupt code and return in how many µs the next event is expected
/* the handling of the protocol is done in the IRQ */

DALIState *gbl_driver;

// IRAM_ATTR is intentionally only on the definition below; placing it on
// both the forward declaration and the definition makes GCC complain
// about conflicting section attributes (.iram1.N counters).
static void gpio_interrupt(DALIState *driver);

static void IRAM_ATTR HOT timer_interrupt() // DALIState *driver);
{
  DALIState *driver = gbl_driver;

  switch(driver->state)
  {
    case MS_TX_SECOND_HALF_START_BIT:
      /*
       * Sending start bit
       */
      DALI_SetOutputHigh(driver);
      driver->bitcount = 0;

      // Timer auto reloads and next waits TE
      driver->state = MS_TX_DALI_FORWARD_FRAME;

      break;

    case MS_TX_DALI_FORWARD_FRAME:
      /*
       * Sending coded data bits
       */
      if (driver->forward_frame & 0x80000000)
        DALI_SetOutputHigh(driver);
      else
        DALI_SetOutputLow(driver);

      driver->forward_frame <<= 1;
      driver->bitcount++;

      // End of data bits?
      if (driver->bitcount == 32)
        driver->state = MS_TX_STOP_BITS;

      // Timer auto reloads and next waits TE
      break;

    case MS_TX_STOP_BITS:
      /*
       * Sending stop bits
       */

      // High state to free the bus
      DALI_SetOutputHigh(driver);

      // The first half of the first stop bit has just been output.
      // Do we have to wait for an answer?
      if (driver->waitForAnswer)
      {
        // Elapse until the end of the last half of the second stop bit
        timerAlarm(driver->timer, 4 * TE, false, 0);

        // Reset receive state - bitcount was used by the TX path.
        driver->backward_frame = 0;
        driver->bitcount = 0;
        driver->state = MS_SETTLING_BEFORE_BACKWARD;
      }
      else
      {
        // No answer from slave expected, need to wait for the remaining
        // bus idle time before next forward frame.
        // Add additional 3 TE to minimum specification to be not at the edge of the timing specification
        timerAlarm(driver->timer, (4 + 22 + 3) * TE, false, 0);
        driver->state = MS_SETTLING_BEFORE_IDLE;
      }
      break;

    case MS_SETTLING_BEFORE_BACKWARD:
      /*
       * Settling: 4*TE after the last forward-frame half-bit.
       */

      DALI_SetOutputHigh(driver);

      // Setup the first window limit for the slave answer.
      // The slave should not respond before 7TE.
      timerAlarm(driver->timer, 7 * MIN_TE, false, 0);

      // The RX ISR is permanently attached (see setup()); state gating
      // means it ignores edges in all states except MS_WAITING_FOR_*
      // and MS_RECEIVING_ANSWER. No attach_interrupt() call here -
      // that's not ISR-safe.

      driver->state = MS_WAITING_FOR_SLAVE_START_WINDOW;
      break;

    case MS_WAITING_FOR_SLAVE_START_WINDOW:
      /*
       *
       */

      // Setup the second window limit for the slave answer,
      // slave must start transmit within the next 23TE window.
      timerAlarm(driver->timer, 23 * MAX_TE, false, 0);
      driver->state = MS_WAITING_FOR_SLAVE_START;

      // Next we should receive GPIO interrupts
      break;

    case MS_WAITING_FOR_SLAVE_START:
      /*
       * Receive timeout. Nothing was received in the slave window.
       */

      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    case MS_RECEIVING_ANSWER:
      /*
       * Receive timeout in middle of frame.
       */

      // Idle the bus between backward and next forward frame. Since we
      // don't track the last edge of the received frame, conservatively
      // wait for 23 TE (>= the 22 TE specification minimum).
      timerAlarm(driver->timer, 23 * MAX_TE, false, 0);
      DALI_SetOutputHigh(driver);
      driver->state = MS_SETTLING_BEFORE_IDLE;
      break;

    case MS_SETTLING_BEFORE_IDLE:
      /*
       * Settling complete: bus is considered idle/available again.
       */

      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    default:
      /*
       * Unexpected timer in idle / TX states - reset.
       */
      DALI_SetOutputHigh(driver);
      timerStop(driver->timer);
      driver->state = MS_IDLE;
  }
}


static void IRAM_ATTR HOT gpio_interrupt(DALIState *driver)
{
  switch (driver->state) {
    case MS_WAITING_FOR_SLAVE_START_WINDOW:
      /*
       * The slave should not have answered yet, it came too early.
       */
      timerStop(driver->timer);
      driver->state = MS_IDLE;
      break;

    case MS_WAITING_FOR_SLAVE_START: {
      /*
       * We got an edge (he first half bit of START), so the slave is transmitting now.
       */

      // Set receive timeout time.
      timerAlarm(driver->timer, 22 * MAX_TE, false, 0);

      // Clear timer counter so we can measure the pulse length
      timerWrite(driver->timer, 0);

      driver->backward_frame <<= 1;

      bool DaliBusLevel = DALI_GetInput(driver);
      if (DaliBusLevel)
        driver->backward_frame |= BI_PHASE_HIGH;

      // Increment bit counter by 1
      driver->bitcount += 1;
      driver->state = MS_RECEIVING_ANSWER;
      break;
    }
    case MS_RECEIVING_ANSWER: {
      /*
       * Received a next edge
       */

      // Get current bus level and the length of the pulse
      bool DaliBusLevel = DALI_GetInput(driver);
      uint32_t CT = timerRead(driver->timer);
      timerWrite(driver->timer, 0);

      // Check the pulse width for TE or 2TE time
      if (MIN_2TE < CT && CT < MAX_2TE)
      {
        // This is a 2TE pulse, we need to shift to bits shift both bits to the left...
        driver->backward_frame <<= 2;

        if (DaliBusLevel)
          driver->backward_frame |= BI_PHASE_HIGH;
        else
          driver->backward_frame |= BI_PHASE_LOW;

        // increment position counter by 2
        driver->bitcount += 2;
      }
      else if (MIN_TE < CT && CT < MAX_TE)
      {
        // This is a TE pulse, we just need to shift one bit == current LEVEL
        // shift one bit to the left...
        driver->backward_frame <<= 1;

        if (DaliBusLevel)
          driver->backward_frame |= BI_PHASE_HIGH;

        // Increment position counter by 1
        driver->bitcount += 1;
      }
      else
      {
        // Pulse is not a valid DALI bi-phase pulse. Abort the receive
        // and let the host wait-loop see MS_IDLE.
        timerStop(driver->timer);
        driver->bitcount = 0;
        driver->backward_frame = 0;
        driver->state = MS_IDLE;
        break;
      }

      // Check if we have received all bits...
      if (driver->bitcount == BACKWARD_FRAME_BIT_LENGTH)
      {
        // Frame complete - wait for the post-frame idle time, then go
        // back to MS_IDLE via the settling state.
        timerWrite(driver->timer, 0);
        timerAlarm(driver->timer, 22 * MAX_TE, false, 0);
        driver->state = MS_SETTLING_BEFORE_IDLE;
      }

      break;
    }

    default:
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
  driver->waitForAnswer = false;

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

uint32_t DALIInterface::DALI_Encode(uint16_t forwardFrame)
{
  uint32_t coded = 0;
  for (int i = 15; i >= 0; i--) // MSB to LSB
  {
    if (forwardFrame & (1 << i))
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

bool DALIInterface::DALI_Decode(uint32_t rawData, uint8_t *pBackwardFrame)
{
  // The raw data is built up LSB-first in the ISR: the first received
  // bi-phase pair (data bit 7) ends up in bits[15:14] and the last
  // pair (data bit 0) ends up in bits[1:0]. Decode from LSB up and
  // assemble the result with data bit 0 at the LSB of the output.
  uint8_t backward_frame = 0;
  for (int i = 0; i < 8; i++)
  {
    switch (rawData & BI_PHASE_MASK)
    {
    case BI_PHASE_HIGH:
      backward_frame |= (1u << i);
      break;
    case BI_PHASE_LOW:
      break;
    default:
      return false;
    }
    rawData >>= 2;
  }

  *pBackwardFrame = backward_frame;
  return true;
}


void DALIInterface::search()
{
  // TODO: implement ballast discovery using the INITIALISE / RANDOMISE /
  //       COMPARE / SEARCHADDR / PROGRAM_SHORT_ADDRESS sequence from
  //       IEC 62386-102 section 9.4. See for reference:
  //   https://github.com/sde1000/python-dali/blob/master/examples/find_ballasts.py
  ESP_LOGW(TAG, "search(): not implemented yet");
}

bool DALIInterface::do_transfer_(uint16_t forwardFrame, bool expect_response, uint8_t *response)
{
  DALIState *driver = &driver_;

  if (expect_response && driver->rx_pin == nullptr) {
    ESP_LOGW(TAG, "Query requested but rx_pin is not configured");
    return false;
  }

  driver->state = MS_TX_SECOND_HALF_START_BIT;
  driver->waitForAnswer = expect_response;
  driver->forward_frame = DALI_Encode(forwardFrame);
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

void DALIInterface::send_forward_frame(uint16_t forwardFrame, bool repeat)
{
  do_transfer_(forwardFrame, false, nullptr);
  if (repeat) {
    // DALI configuration commands must be sent twice within 100 ms; the
    // state machine already inserts the >=22 TE settling time between
    // transfers, so back-to-back calls are fine.
    do_transfer_(forwardFrame, false, nullptr);
  }
}

bool DALIInterface::send_forward_frame(uint16_t forwardFrame, uint8_t &response)
{
  return do_transfer_(forwardFrame, true, &response);
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

// All four helpers below send DT8 (colour) commands. DT8 commands must
// (a) be addressed as commands (selector bit S = 1, i.e. | 0x0100) and
// (b) be immediately preceded by ENABLE_DEVICE_TYPE 8, which only affects
// the *next* command - so it must be repeated for every DT8 frame sent.

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
  // IEC 62386-209 §6.13 (cmd 250 QUERY COLOUR VALUE):
  //   1. DTR0 := <query> (selector for which colour value to return)
  //   2. send QUERY COLOUR VALUE; response = MSB of selected value
  //   3. send QUERY CONTENT DTR0; response = LSB of selected value
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

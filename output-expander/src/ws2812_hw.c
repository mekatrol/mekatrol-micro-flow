#include "ws2812.h"

#include "registers.h"

#include <stdint.h>

/*
 * CH32V003 TIM1/DMA WS2812 backend
 * --------------------------------
 *
 * TIM1 channel 2 produces an 800 kHz PWM waveform on PC7. At the 48 MHz
 * peripheral clock, ARR=59 gives 60 counter clocks per PWM period:
 *
 *     48,000,000 / 60 = 800,000 periods/s = 1.25 us/period
 *
 * PWM mode 1 makes CH2 HIGH while TIM1_CNT is less than TIM1_CH2CVR (CCR2),
 * then LOW until the counter rolls over. Compare values 18 and 36 therefore
 * create the WS2812 zero and one HIGH times. DMA replaces CCR2 once per timer
 * update, leaving the CPU free to service USART interrupts and foreground
 * protocol work.
 *
 * CCR2 preload is central to the design. Software/DMA writes go to a shadow
 * register while the active compare value controls the current PWM period.
 * The next update event copies the shadow value into the active register.
 * The output must not be connected while a nonzero active compare is sitting
 * at CNT=0 with the counter stopped: that begins a HIGH interval before the
 * timer starts, and the LED does not accept it as a complete data pulse. Keep
 * zero active for one LOW guard period, place the first duty in the preload
 * register, and let the first real update boundary begin the data stream. DMA
 * begins at duty[0] because the peripheral consumes one request while being
 * armed; that request deliberately rewrites the staged value.
 */

/*
 * Register-layout and timing constants used below. Naming these values keeps
 * the register expressions readable and records their derivation in one place.
 */
enum {
  /* PC7 is pin number 7 within GPIO port C. GPIO bit-set/clear registers use
   * one bit per pin, so its mask is 1 << 7 = 0x00000080. */
  WS2812_GPIO_PIN = 7U,

  /* GPIOC_CFGLR assigns four configuration bits to every pin: MODE[1:0] plus
   * CNF[1:0]. PC7's field therefore begins at 7 * 4 = bit 28 and occupies
   * bits 31:28. This is the source of the former unexplained shift by 28. */
  GPIO_CONFIG_BITS_PER_PIN = 4U,
  WS2812_GPIO_CONFIG_SHIFT = WS2812_GPIO_PIN * GPIO_CONFIG_BITS_PER_PIN,

  /* The prescaler divides the timer clock by PSC+1. A register value of zero
   * means divide by one, retaining the complete 48 MHz peripheral clock. */
  WS2812_TIMER_PRESCALER = 0U,

  /* WS2812 uses an 800 kHz serial bit rate. Dividing 48 MHz by 800 kHz gives
   * 60 timer counts for each data-bit/PWM period. TIM1 counts from zero through
   * ARR inclusive, so ARR must be 60-1 = 59. The resulting period is
   * 60 / 48,000,000 s = 1.25 us. */
  WS2812_TIMER_COUNTS_PER_BIT = 60U,
  WS2812_TIMER_AUTO_RELOAD = WS2812_TIMER_COUNTS_PER_BIT - 1U,

  /* On this MCU, enabling CC2DE consumes one deterministic CH2 DMA request
   * before CEN starts, despite clearing flags and gating CCDS around UG. Feed
   * that priming request duty[0], which is already staged in CCR2, so it cannot
   * overwrite the first data bit with duty[1]. */
  WS2812_DMA_SOURCE_DUTY_INDEX = 0U
};

void ws2812_hw_init(void) {
  /* RCC_AHBPCENR gates clocks to high-speed/AHB peripherals. RCC_DMA1EN sets
   * bit 0, enabling DMA1 register access and transfer operation. Read-modify-
   * write preserves the clock state of every unrelated AHB peripheral. */
  RCC_AHBPCENR |= RCC_DMA1EN;

  /* RCC_APB2PCENR gates APB2 peripherals. Enable:
   *   AFIOEN  (bit 0): alternate-function remapping,
   *   IOPCEN  (bit 4): GPIO port C configuration/output registers,
   *   TIM1EN  (bit 11): advanced timer 1.
   * Other APB2 clocks are preserved. */
  RCC_APB2PCENR |= RCC_AFIOEN | RCC_TIM1EN | RCC_IOPCEN;

  /* AFIO_PCFR1 TIM1_RM[1:0] occupies bits 7:6 and selects the physical pins
   * used by TIM1 channels. Clear only those two bits, then select remap group
   * 1 (binary 01). On CH32V003 this routes TIM1_CH2 to PC7. Every unrelated
   * USART, I2C, SPI, and debug remap field remains unchanged. */
  AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_TIM1_RM_MASK) | AFIO_TIM1_RM_GROUP1;

  /* GPIOC_BCR is the atomic bit-clear register. Writing bit 7 clears PC7's
   * output data latch without a read-modify-write race. Preloading LOW avoids
   * a spurious HIGH edge when ownership changes from GPIO to TIM1. */
  GPIOC_BCR = UINT32_C(1) << WS2812_GPIO_PIN;

  /* GPIOC_CFGLR contains a four-bit configuration field for each pin 0..7;
   * PC7 uses bits 31:28 because 7 pins * 4 bits/pin = a shift of 28. The
   * GPIO_CONFIG_FIELD_MASK value is 0b1111 (0xF), which selects exactly one
   * four-bit field before it is shifted to PC7's position. Clear that field,
   * then write GPIO_AF_OUTPUT_50MHZ, whose value is 0b1011 (0xB):
   *   MODE7[1:0] = 11: output mode, maximum 50 MHz slew capability,
   *   CNF7[1:0]  = 10: alternate-function push-pull.
   * Push-pull is required because the WS2812 data input needs both actively
   * driven HIGH and actively driven LOW timing edges. */
  GPIOC_CFGLR =
      (GPIOC_CFGLR & ~(GPIO_CONFIG_FIELD_MASK << WS2812_GPIO_CONFIG_SHIFT)) |
      (GPIO_AF_OUTPUT_50MHZ << WS2812_GPIO_CONFIG_SHIFT);

  /* Stop TIM1 and clear all CTLR1 modes before programming it. In particular,
   * CEN (bit 0) is zero, so the counter cannot run during setup. */
  TIM1_CTLR1 = 0U;

  /* TIM1_CTLR2.CCDS (bit 3) redirects the channel-2 DMA request to timer
   * update boundaries. CH2 therefore advances once per complete PWM period
   * while retaining its DMA1 channel 3 routing. */
  TIM1_CTLR2 = TIM_CTLR2_CCDS;

  /* TIM1_DMAINTENR controls timer interrupt and DMA request generation. Start
   * with every source disabled. CC2DE (bit 10) is enabled only after DMA1_CH3
   * has a valid address and count in ws2812_hw_start(). */
  TIM1_DMAINTENR = 0U;

  /* TIM1_CCER controls whether each timer channel reaches its output pin and
   * also controls polarity. Zero disables CH2 while retaining normal active-
   * HIGH polarity for when CC2E is later set. */
  TIM1_CCER = 0U;

  /* PSC divides the timer input clock by PSC+1. PSC=0 preserves the full
   * 48 MHz timer clock, giving one counter tick every 20.833 ns. */
  TIM1_PSC = WS2812_TIMER_PRESCALER;

  /* ATRLR is the auto-reload/period register (ARR). This "period" is one
   * complete WS2812 serial data bit and therefore one PWM cycle. With PSC=0,
   * TIM1 receives the 48 MHz peripheral clock. The calculation is:
   *
   *     counts/bit = 48,000,000 clocks/s / 800,000 bits/s = 60 counts
   *     ARR        = counts/bit - 1 = 59
   *     bit period = 60 / 48,000,000 s = 1.25 us.
   *
   * The subtraction is required because an up-counter includes both CNT=0 and
   * CNT=59 before rolling over, which is 60 distinct clock counts. */
  TIM1_ATRLR = WS2812_TIMER_AUTO_RELOAD;

  /* RPTCR is TIM1's advanced-timer repetition counter. Zero requests an update
   * for every PWM period; a nonzero value would incorrectly hold duties for
   * multiple WS2812 bits before generating the next DMA request. */
  TIM1_RPTCR = 0U;

  /* TIM1_CHCTLR1 holds the output-compare configuration for channels 1 and 2.
   * For channel 2, OC2M[2:0] is bits 14:12 and value 110 selects PWM mode 1;
   * OC2PE is bit 11 and enables CCR2 preload. All input-capture and fast-output
   * options remain disabled. */
  TIM1_CHCTLR1 = TIM_CHCTLR1_OC2M_PWM1 | TIM_CHCTLR1_OC2PE;

  /* A zero compare produces a continuously LOW PWM period. Use it as the safe
   * initial value before any frame has been requested. */
  TIM1_CH2CVR = 0U;

  /* TIM1 is an advanced timer, so channel enables alone are insufficient.
   * BDTR.MOE (bit 15), the main-output enable, opens the final output gate.
   * Break/dead-time/lock features remain disabled because this is a single
   * logic waveform rather than a complementary motor-control output. */
  TIM1_BDTR = TIM_BDTR_MOE;

  /* SWEVGR.UG (bit 0) generates a software update event. It loads PSC/ARR and
   * the zero CCR2 preload into their active timer registers without waiting
   * for a real counter overflow. UG also sets status flags, cleared below. */
  TIM1_SWEVGR = TIM_SWEVGR_UG;

  /* Clear all timer status flags left by reset/setup/UG. This backend polls the
   * DMA flag rather than timer flags, but stale flags complicate debugging and
   * could become significant if interrupts are added later. */
  TIM1_INTFR = 0U;
}

void ws2812_hw_start(const ws2812_waveform_t *waveform) {
  /* Re-establish a fully stopped, request-free baseline. These operations also
   * make restart safe after the previous frame's reset interval:
   *   CTLR1.CEN=0 stops counting,
   *   DMAINTENR.CC2DE=0 stops new TIM1_CH2 DMA requests,
   *   DMA_CFGR.EN=0 permits DMA address/count reconfiguration. */
  TIM1_CTLR1 &= ~TIM_CTLR1_CEN;
  TIM1_DMAINTENR &= ~TIM_DMAINTENR_CC2DE;
  DMA1_CH3_CFGR &= ~DMA_CFGR_EN;

  /* DMA1_INTFCR is write-one-to-clear. Bits 11:8 clear channel 3's transfer
   * error, half-transfer, transfer-complete, and global flags. Clearing TCIF3
   * here prevents an old frame from appearing immediately complete. */
  DMA1_INTFCR = DMA_CH3_CLEAR_MASK;

  /* Stage a zero-duty guard. Because OC2PE is enabled, this write initially
   * enters CCR2's preload/shadow register. The following UG makes it active. */
  TIM1_CH2CVR = 0U;

  /* Keep one update per period and restart the counter at the beginning of a
   * PWM cell. CNT=0 is important: enabling midway through a period would make
   * the first WS2812 bit shorter than 1.25 us. */
  TIM1_RPTCR = 0U;
  TIM1_CNT = 0U;

  /* UG transfers the zero-duty guard from the CCR2 preload register to the
   * active compare register and reloads the prescaler. TIM1 remains stopped,
   * so no pulse is emitted by this software-generated update. */
  TIM1_SWEVGR = TIM_SWEVGR_UG;

  /* UG sets UIF and may set other status state. Clear it before the real frame;
   * DMA request generation is still disabled, so UG cannot consume a duty. */
  TIM1_INTFR = 0U;

  /* Leave duty[0] in the CCR2 preload register while zero remains active. The
   * first real overflow promotes duty[0], so its rising edge begins exactly on
   * a timer update boundary. The priming DMA request safely rewrites this same
   * value before real update requests advance to duty[1]. */
  TIM1_CH2CVR = waveform->duty[0];

  /* PADDR is DMA channel 3's fixed peripheral address. It points at TIM1 CCR2,
   * represented here by TIM1_CH2CVR. Peripheral increment is not enabled, so
   * every transfer writes another 16-bit duty to this same register. */
  DMA1_CH3_PADDR = (uint32_t)(uintptr_t)&TIM1_CH2CVR;

  /* MADDR is the source address in SRAM. DMA intentionally begins at duty[0]:
   * the deterministic request generated when CC2DE is enabled rewrites the
   * already-staged duty[0] instead of prematurely advancing to duty[1]. After
   * that priming transfer, real update boundaries consume duty[1] onward.
   * Memory increment then proceeds from duty[23] into trailing_zero[0]. */
  DMA1_CH3_MADDR =
      (uint32_t)(uintptr_t)&waveform->duty[WS2812_DMA_SOURCE_DUTY_INDEX];

  /* CNTR includes the duty[0] priming rewrite, all subsequent data duties, and
   * the complete zero tail: 24 + 16 = 40 halfword transfers. The pre-start
   * request should reduce this to 39 before CEN; no data entry is then lost. */
  DMA1_CH3_CNTR = WS2812_BIT_COUNT + WS2812_TAIL_COUNT;

  /* Configure DMA channel 3:
   *   DIR       (bit 4)  = 1: read memory, write peripheral,
   *   MINC      (bit 7)  = 1: advance the source after each duty,
   *   PSIZE     (9:8)    = 01: 16-bit peripheral write,
   *   MSIZE     (11:10)  = 01: 16-bit memory read,
   *   PL        (13:12)  = 10: high arbitration priority.
   * EN remains zero until the full configuration is committed. Circular mode,
   * peripheral increment, memory-to-memory mode, and DMA interrupts are off. */
  DMA1_CH3_CFGR = DMA_CFGR_DIR | DMA_CFGR_MINC | DMA_CFGR_PSIZE_16 |
                  DMA_CFGR_MSIZE_16 | DMA_CFGR_PL_HIGH;

  /* CCER.CC2E (bit 4) connects TIM1 channel 2 to the remapped PC7 output. The
   * counter is still stopped, so enabling the channel cannot start a partial
   * pulse. Normal polarity means PWM HIGH is a physical HIGH on PC7. */
  TIM1_CCER = TIM_CCER_CC2E;

  /* Arm the consumer before its request source. DMA_CFGR.EN makes channel 3
   * ready, then DMAINTENR.CC2DE permits TIM1 CH2 update-phased requests. This
   * ordering prevents a timer request from arriving at a disabled channel. */
  DMA1_CH3_CFGR |= DMA_CFGR_EN;
  TIM1_DMAINTENR |= TIM_DMAINTENR_CC2DE;

  /* Start TIM1 by setting CTLR1.CEN (bit 0). ARPE (bit 7) enables ARR preload,
   * keeping the 60-clock period stable at update boundaries. Assignment is
   * intentional: all other counting modes remain cleared (up-counting,
   * edge-aligned, continuous operation, updates enabled). */
  TIM1_CTLR1 = TIM_CTLR1_ARPE | TIM_CTLR1_CEN;
}

bool ws2812_hw_complete(void) {
  /* DMA1_INTFR.TCIF3 (bit 9) is set after CNTR reaches zero. It is a sticky
   * hardware flag, so foreground polling cannot miss a short event. With the
   * 16-period zero tail included in CNTR, true means every data duty and ample
   * LOW padding have already been supplied to TIM1. */
  return (DMA1_INTFR & DMA_CH3_TCIF) != 0U;
}

void ws2812_hw_force_low(void) {
  /* First stop additional timer requests, then disable DMA channel 3. This
   * prevents a late request from racing peripheral shutdown. */
  TIM1_DMAINTENR &= ~TIM_DMAINTENR_CC2DE;
  DMA1_CH3_CFGR &= ~DMA_CFGR_EN;

  /* Stop the counter before disconnecting the output. The DMA tail has held
   * CCR2 at zero, so PC7 is already LOW and no active HIGH pulse is shortened.
   */
  TIM1_CTLR1 &= ~TIM_CTLR1_CEN;

  /* Clear CCER.CC2E to disconnect TIM1_CH2 from PC7. GPIOC remains configured
   * for alternate-function push-pull, but its previously cleared output latch
   * provides the safe LOW level once the timer channel is disabled. */
  TIM1_CCER &= ~TIM_CCER_CC2E;

  /* Leave both the compare register and GPIO latch explicitly LOW. This makes
   * reset/latch time independent of residual timer state and gives a safe
   * starting point if a debugger inspects or restarts the peripheral. */
  TIM1_CH2CVR = 0U;
  GPIOC_BCR = UINT32_C(1) << WS2812_GPIO_PIN;

  /* Clear timer status and every DMA channel-3 flag. The next frame also clears
   * DMA flags defensively before configuration, but cleaning up here documents
   * and enforces the idle-state invariant. */
  TIM1_INTFR = 0U;
  DMA1_INTFCR = DMA_CH3_CLEAR_MASK;
}

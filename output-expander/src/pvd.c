#include "pvd.h"

#include "hal.h"
#include "registers.h"

#include <stdbool.h>
#include <stdint.h>

/* PLS=001 selects 3.05 V on recovery and 2.9 V on a falling supply. The
 * detector's built-in hysteresis prevents chatter around a single threshold. */
#define PVD_PLS_3V05 (UINT32_C(1) << 5)

static volatile bool pvd_brownout_latched;

static void pvd_enter_safe_state(void) {
  pvd_brownout_latched = true;
  hal_output_off(UINT8_MAX);
}

void pvd_init(void) {
  /* PWR registers are inaccessible until their APB1 clock is enabled. */
  RCC_APB1PCENR |= RCC_PWREN;

  /* Configure the threshold while the detector and its interrupt are off. */
  PWR_CTLR =
      (PWR_CTLR & ~(PWR_CTLR_PLS_MASK | PWR_CTLR_PVDE)) | PVD_PLS_3V05;

  /* PVD output goes from zero to one when VDD falls below the threshold, so
   * EXTI8 uses only its rising edge. Clear any stale flag before unmasking. */
  EXTI_INTENR &= ~EXTI_PVD_LINE;
  EXTI_FTENR &= ~EXTI_PVD_LINE;
  EXTI_RTENR |= EXTI_PVD_LINE;
  EXTI_INTFR = EXTI_PVD_LINE;

  /* IRQ 17 is bit 17 of PFIC enable register 1. IENR1 is write-one-to-enable. */
  PFIC_IENR1 = PVD_IRQ_ENABLE_MASK;
  EXTI_INTENR |= EXTI_PVD_LINE;
  PWR_CTLR |= PWR_CTLR_PVDE;

  /* Enabling below an already-low supply need not create a new edge. Apply
   * the same fail-safe action synchronously so startup is safe as well. */
  if ((PWR_CSR & PWR_CSR_PVDO) != 0U) {
    pvd_enter_safe_state();
  }
}

bool pvd_brownout_detected(void) { return pvd_brownout_latched; }

void pvd_irq_handler(void) {
  /* De-energize every output before acknowledging the event. GPIO BSHR writes
   * are bounded and atomic, making this safe and deterministic in the ISR. */
  pvd_enter_safe_state();
  EXTI_INTFR = EXTI_PVD_LINE;
}

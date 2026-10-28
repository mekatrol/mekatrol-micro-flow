
#include "watchdog.h"

#include <stdint.h>

/*
 * CH32V003 RCC control/status register.
 *
 * WHAT:
 * Enable and monitor the independent 128 kHz LSI oscillator.
 *
 * WHY:
 * Unlike the main 48 MHz system clock, LSI continues
 * driving the independent watchdog even if normal
 * application execution stalls.
 *
 * The RCC reset flags share this register. Setting
 * LSION by read-modify-write preserves those flags.
 *
 * Do not write the RMVF reset-flag-clearing bit here.
 */
#define RCC_RSTSCKR (*(volatile uint32_t *)(uintptr_t)UINT32_C(0x40021024))

#define RCC_LSION UINT32_C(0x00000001)
#define RCC_LSIRDY UINT32_C(0x00000002)

/*
 * Independent-watchdog 16-bit register map.
 *
 * CTLR: key/control register
 * PSCR: prescaler selection
 * RLDR: reload value
 * STATR: register update status
 *
 * The device uses 16-bit accesses for these registers.
 * Volatile prevents the compiler from eliminating or
 * merging the required memory-mapped operations.
 */
#define IWDG_CTLR (*(volatile uint16_t *)(uintptr_t)UINT32_C(0x40003000))

#define IWDG_PSCR (*(volatile uint16_t *)(uintptr_t)UINT32_C(0x40003004))

#define IWDG_RLDR (*(volatile uint16_t *)(uintptr_t)UINT32_C(0x40003008))

#define IWDG_STATR (*(volatile const uint16_t *)(uintptr_t)UINT32_C(0x4000300C))

/*
 * Hardware-defined control keys.
 *
 * 0x5555 unlocks prescaler and reload updates.
 * 0xCCCC starts the independent watchdog.
 * 0xAAAA reloads its downcounter (feeds it).
 */
#define IWDG_KEY_UNLOCK UINT16_C(0x5555)
#define IWDG_KEY_START UINT16_C(0xCCCC)
#define IWDG_KEY_RELOAD UINT16_C(0xAAAA)

/*
 * Prescaler encoding 5 means LSI / 128.
 *
 * Maximum reload is 0x0FFF because the hardware
 * downcounter and reload field are 12 bits wide.
 */
#define IWDG_PRESCALER_128 UINT16_C(5)
#define IWDG_RELOAD_VALUE UINT16_C(0x0FFF)

/*
 * STATR indicates synchronization of new configuration
 * values into the independent LSI clock domain.
 *
 * PVU: prescaler value update in progress
 * RVU: reload value update in progress
 *
 * Wait for both to clear before starting the watchdog.
 */
#define IWDG_STATR_PVU UINT16_C(0x0001)
#define IWDG_STATR_RVU UINT16_C(0x0002)

void watchdog_start(void) {
  /*
   * Enable LSI without disturbing the existing system
   * oscillator or the verified AHB prescaler configuration.
   *
   * LSI is independent of HSI and PLL.
   */
  RCC_RSTSCKR |= RCC_LSION;

  /*
   * Wait for the hardware to declare LSI stable.
   *
   * No watchdog feeds occur during this initialization.
   * This is expected to complete promptly with healthy
   * hardware.
   *
   * If LSI never starts, initialization cannot safely
   * claim that watchdog protection is operational.
   */
  while ((RCC_RSTSCKR & RCC_LSIRDY) == 0U) {
  }

  /*
   * Unlock writes to watchdog prescaler and reload.
   *
   * The hardware locks these registers against
   * accidental configuration changes.
   */
  IWDG_CTLR = IWDG_KEY_UNLOCK;

  /*
   * Configure a 4.096-second nominal watchdog period:
   *
   * LSI:        128000 Hz nominal
   * Prescaler:  128
   * Tick rate:  1000 Hz nominal
   * Reload:     4095
   *
   * Nominal timeout = 4096 / 1000 = 4.096 seconds.
   *
   * The actual timeout varies with LSI frequency.
   * This value leaves room for normal blocking
   * response transmission and foreground processing.
   */
  IWDG_PSCR = IWDG_PRESCALER_128;
  IWDG_RLDR = IWDG_RELOAD_VALUE;

  /*
   * Wait until both configuration transfers have
   * completed in the independent watchdog clock domain.
   *
   * Starting before synchronization could use an old
   * prescaler or reload value and yield an unexpected
   * reset interval.
   */
  while ((IWDG_STATR & (IWDG_STATR_PVU | IWDG_STATR_RVU)) != 0U) {
  }

  /*
   * Start IWDG, then explicitly load the configured
   * reload value into its counter.
   *
   * After START, software cannot stop the watchdog.
   * Only ongoing refreshes prevent reset.
   */
  IWDG_CTLR = IWDG_KEY_START;
  IWDG_CTLR = IWDG_KEY_RELOAD;
}

void watchdog_feed(void) {
  /*
   * A single key write reloads the running downcounter.
   *
   * No additional RCC, timer or peripheral operation
   * is necessary.
   *
   * The caller controls whether feeds are permitted.
   * The watchdog driver intentionally does not keep
   * a global "feed enabled" flag, because that policy
   * belongs to the foreground communications application.
   */
  IWDG_CTLR = IWDG_KEY_RELOAD;
}

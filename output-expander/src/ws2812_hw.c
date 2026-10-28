#include "ws2812.h"

#include "registers.h"

#include <stdint.h>

void ws2812_hw_init(void) {
  RCC_AHBPCENR |= RCC_DMA1EN;
  RCC_APB2PCENR |= RCC_AFIOEN | RCC_TIM1EN | RCC_IOPCEN;

  /* TIM1 remap group 1 places CH2 on PC7. Preserve every unrelated remap. */
  AFIO_PCFR1 = (AFIO_PCFR1 & ~AFIO_TIM1_RM_MASK) | AFIO_TIM1_RM_GROUP1;

  /* Keep the output latch LOW while changing PC7 to TIM1 alternate output. */
  GPIOC_BCR = UINT32_C(1) << 7;
  GPIOC_CFGLR = (GPIOC_CFGLR & ~(GPIO_CONFIG_FIELD_MASK << 28)) |
                (GPIO_AF_OUTPUT_50MHZ << 28);

  TIM1_CTLR1 = 0U;
  TIM1_CTLR2 = TIM_CTLR2_CCDS;
  TIM1_DMAINTENR = 0U;
  TIM1_CCER = 0U;
  TIM1_PSC = 0U;
  TIM1_ATRLR = 59U;
  TIM1_RPTCR = 0U;
  TIM1_CHCTLR1 = TIM_CHCTLR1_OC2M_PWM1 | TIM_CHCTLR1_OC2PE;
  TIM1_CH2CVR = 0U;
  TIM1_BDTR = TIM_BDTR_MOE;
  TIM1_SWEVGR = TIM_SWEVGR_UG;
  TIM1_INTFR = 0U;
}

void ws2812_hw_start(const ws2812_waveform_t *waveform) {
  TIM1_CTLR1 &= ~TIM_CTLR1_CEN;
  TIM1_DMAINTENR &= ~TIM_DMAINTENR_CC2DE;
  DMA1_CH3_CFGR &= ~DMA_CFGR_EN;
  DMA1_INTFCR = DMA_CH3_CLEAR_MASK;

  TIM1_CH2CVR = waveform->duty[0];
  TIM1_RPTCR = 0U;
  TIM1_CNT = 0U;
  TIM1_SWEVGR = TIM_SWEVGR_UG;
  TIM1_INTFR = 0U;
  TIM1_CH2CVR = waveform->duty[1];

  DMA1_CH3_PADDR = (uint32_t)(uintptr_t)&TIM1_CH2CVR;
  DMA1_CH3_MADDR = (uint32_t)(uintptr_t)&waveform->duty[2];
  DMA1_CH3_CNTR = 24U;
  DMA1_CH3_CFGR = DMA_CFGR_DIR | DMA_CFGR_MINC | DMA_CFGR_PSIZE_16 |
                  DMA_CFGR_MSIZE_16 | DMA_CFGR_PL_HIGH;

  TIM1_CCER = TIM_CCER_CC2E;
  DMA1_CH3_CFGR |= DMA_CFGR_EN;
  TIM1_DMAINTENR |= TIM_DMAINTENR_CC2DE;
  TIM1_CTLR1 = TIM_CTLR1_ARPE | TIM_CTLR1_CEN;
}

bool ws2812_hw_complete(void) {
  return (DMA1_INTFR & DMA_CH3_TCIF) != 0U;
}

void ws2812_hw_force_low(void) {
  TIM1_DMAINTENR &= ~TIM_DMAINTENR_CC2DE;
  DMA1_CH3_CFGR &= ~DMA_CFGR_EN;
  TIM1_CTLR1 &= ~TIM_CTLR1_CEN;
  TIM1_CCER &= ~TIM_CCER_CC2E;
  TIM1_CH2CVR = 0U;
  GPIOC_BCR = UINT32_C(1) << 7;
  TIM1_INTFR = 0U;
  DMA1_INTFCR = DMA_CH3_CLEAR_MASK;
}

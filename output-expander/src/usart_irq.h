
#ifndef OUTPUT_EXPANDER_USART_IRQ_H
#define OUTPUT_EXPANDER_USART_IRQ_H

/*
 * USART1 interrupt handler called by startup.S.
 *
 * WHAT:
 * Reads USART1 receive status and transfers
 * received bytes or error events to the RX queue.
 *
 * WHY:
 * The QingKe interrupt entry wrapper must preserve
 * interrupted CPU registers before executing C code.
 *
 * This function is called from usart1_irq_entry
 * in startup.S, not directly from main().
 *
 * The assembly wrapper is responsible for returning
 * from the interrupt using the mret instruction.
 */
void usart1_irq_handler(void);

#endif

//! Foreground processing of timestamped UART receive events.
//!
//! The interrupt handler queues received bytes and UART errors.
//! The foreground worker transfers those events into the Modbus RTU
//! receiver and checks whether a frame has completed.

use crate::irq_rx::{RxEvent, RxQueue};
use crate::rtu_receiver::RtuReceiver;

/// Transfer queued UART events into the Modbus RTU receiver.
///
/// `now_ticks` is the current monotonic hardware counter value.
/// It must use the same tick rate as the receiver.
///
/// Queue overflow and UART errors require recovery because the
/// received byte stream can no longer be trusted.
pub fn drain_events(queue: &mut RxQueue, receiver: &mut RtuReceiver, now_ticks: u32) {
    // An overflow means at least one UART event was lost.
    // Clear the queue and discard the incomplete RTU frame.
    if queue.recover_overflow() {
        receiver.on_stream_loss(now_ticks);
    }

    // Process surviving events in their original arrival order.
    while let Some(event) = queue.pop() {
        match event {
            RxEvent::Byte { value, ticks } => {
                // Preserve the byte's original interrupt timestamp.
                receiver.on_byte(value, ticks);
            }

            RxEvent::Error { ticks } => {
                // Recover using the timestamp captured by the interrupt,
                // not the potentially later foreground processing time.
                receiver.on_uart_error(ticks);
            }
        }
    }

    // Check for a completed RTU frame after processing queued events.
    receiver.poll(now_ticks);
}

//! Foreground processing of timestamped UART receive events.
//!
//! The interrupt handler queues received bytes and UART errors.
//! The foreground worker transfers those events into the Modbus RTU
//! receiver and checks whether a frame has completed.

// Hardware-independent UART event and queue types.
use crate::irq_rx::{RxEvent, RxQueue, RxQueueRead};

// The global interrupt queue is accessed only by the RISC-V firmware.
#[cfg(target_arch = "riscv32")]
use crate::irq_rx;
use crate::rtu_receiver::RtuReceiver;

// Read the same free-running SysTick counter used by the USART interrupt.
#[cfg(target_arch = "riscv32")]
use crate::monotonic::current_ticks;

/// Transfer queued UART events into the Modbus RTU receiver.
///
/// Each queue read checks for overflow before returning an event.
/// On overflow, the receiver discards its incomplete frame and
/// waits for an RTU t3.5 silent interval before accepting new data.
pub fn drain_events(queue: &mut RxQueue, receiver: &mut RtuReceiver, now_ticks: u32) {
    loop {
        match queue.read_next() {
            RxQueueRead::Event(RxEvent::Byte { value, ticks }) => {
                // Process the byte using its interrupt timestamp.
                receiver.on_byte(value, ticks);
            }

            RxQueueRead::Event(RxEvent::Error { ticks }) => {
                // Discard the damaged frame after a UART error.
                receiver.on_uart_error(ticks);
            }

            RxQueueRead::Overflow => {
                // Some events were lost. Discard the incomplete frame
                // and require a new silent interval for recovery.
                receiver.on_stream_loss(now_ticks);
            }

            RxQueueRead::Empty => {
                // No further events are available in this snapshot.
                break;
            }
        }
    }

    // Publish a complete frame only after the required silence.
    receiver.poll(now_ticks);
}

/// Drain USART events and complete RTU framing in the foreground.
///
/// The final queue check and frame-completion poll are performed
/// atomically with respect to the USART receive interrupt.
#[cfg(target_arch = "riscv32")]
pub fn drain_global_events(receiver: &mut RtuReceiver) {
    loop {
        // Retrieve an event while protecting the queue from
        // concurrent interrupt modifications.
        let result = irq_rx::read_next_from_main();

        match result {
            RxQueueRead::Event(RxEvent::Byte { value, ticks }) => {
                // Preserve the byte's original interrupt timestamp.
                receiver.on_byte(value, ticks);
            }

            RxQueueRead::Event(RxEvent::Error { ticks }) => {
                // Preserve the existing error-handling behavior.
                receiver.on_uart_error(ticks);
            }

            RxQueueRead::Overflow => {
                // Lost UART events require a new silent interval.
                receiver.on_stream_loss(current_ticks());
            }

            RxQueueRead::Empty => {
                // Verify that the queue is still empty and poll the
                // receiver without allowing a new USART interrupt
                // between those two operations.
                let finished = irq_rx::with_rx_queue_locked(|queue| {
                    match queue.read_next() {
                        RxQueueRead::Empty => {
                            receiver.poll(current_ticks());
                            true
                        }

                        RxQueueRead::Overflow => {
                            // A concurrent overflow was detected.
                            // Discard the incomplete transmission.
                            receiver.on_stream_loss(current_ticks());
                            false
                        }

                        RxQueueRead::Event(event) => {
                            // An event arrived after the previous
                            // empty-queue observation. Process it
                            // before attempting frame completion.
                            match event {
                                RxEvent::Byte { value, ticks } => {
                                    receiver.on_byte(value, ticks);
                                }

                                RxEvent::Error { ticks } => {
                                    receiver.on_uart_error(ticks);
                                }
                            }

                            false
                        }
                    }
                });

                if finished {
                    break;
                }
            }
        }
    }
}

/// Copy a completed Modbus RTU request after checking queue health.
///
/// The queue check and frame handoff occur within one critical
/// section, preventing the USART interrupt from modifying queue
/// state between those operations.
///
/// Returns the request length when a completed frame is available.
/// Returns `None` when no frame is ready or receive data was lost.
///
/// This function must not execute Modbus commands or transmit UART
/// data while interrupts are disabled.
#[cfg(target_arch = "riscv32")]
pub fn take_global_ready_frame(
    receiver: &mut RtuReceiver,
    destination: &mut [u8],
) -> Option<usize> {
    irq_rx::with_rx_queue_locked(|queue| {
        match queue.read_next() {
            RxQueueRead::Overflow => {
                // An overflow invalidates the receive stream.
                // Conservatively discard any pending completed frame.
                receiver.invalidate_pending_frames(current_ticks());
                None
            }

            RxQueueRead::Event(event) => {
                // Process an event that arrived after the previous
                // foreground drain, before attempting the handoff.
                match event {
                    RxEvent::Byte { value, ticks } => {
                        receiver.on_byte(value, ticks);
                    }

                    RxEvent::Error { ticks } => {
                        receiver.on_uart_error(ticks);
                    }
                }

                // The queue may contain additional events.
                // Drain them before deciding whether a frame is ready.
                None
            }

            RxQueueRead::Empty => {
                // No queued event or overflow can intervene between
                // this check and the completed-frame copy.
                receiver.take_ready_frame(destination)
            }
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // RTU frame completion requires a t3.5 silent interval.
    use crate::timer::T3_5_US;

    // Queue capacity is used to simulate receive-buffer overflow.
    use crate::irq_rx::RX_QUEUE_CAPACITY;

    /// Queue one byte with its original interrupt timestamp.
    fn queue_byte(queue: &mut RxQueue, value: u8, ticks: u32) {
        assert!(queue.push(RxEvent::Byte { value, ticks }));
    }

    /// Queue a complete four-byte frame with valid RTU timing.
    ///
    /// These bytes test framing, not CRC validation.
    fn queue_frame(queue: &mut RxQueue, start: u32) {
        queue_byte(queue, 0x11, start);
        queue_byte(queue, 0x03, start + 100);
        queue_byte(queue, 0x00, start + 200);
        queue_byte(queue, 0x00, start + 300);
    }

    #[test]
    fn queued_bytes_preserve_interrupt_timestamps() {
        let mut queue = RxQueue::new();
        let mut receiver = RtuReceiver::new();

        // The foreground worker runs much later than byte arrival.
        // Frame completion must use the original byte timestamps.
        queue_frame(&mut queue, 100);

        drain_events(&mut queue, &mut receiver, 400 + T3_5_US);

        assert_eq!(
            receiver.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }

    #[test]
    fn uart_error_discards_until_silence() {
        let mut queue = RxQueue::new();
        let mut receiver = RtuReceiver::new();

        // Begin a frame, then report a UART error.
        queue_byte(&mut queue, 0x11, 100);
        queue_byte(&mut queue, 0x03, 200);
        assert!(queue.push(RxEvent::Error { ticks: 300 }));

        // Remaining bytes belong to the damaged transmission.
        queue_byte(&mut queue, 0x00, 400);
        queue_byte(&mut queue, 0x00, 500);

        drain_events(&mut queue, &mut receiver, 500 + T3_5_US);

        // The damaged frame must never be published.
        assert!(!receiver.frames().has_ready_frame());

        // A new frame after sufficient silence is accepted.
        queue_frame(&mut queue, 500 + T3_5_US + 100);

        drain_events(&mut queue, &mut receiver, 500 + T3_5_US + 400 + T3_5_US);

        assert_eq!(
            receiver.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }

    #[test]
    fn queue_overflow_discards_damaged_stream() {
        let mut queue = RxQueue::new();
        let mut receiver = RtuReceiver::new();

        // Fill the queue beyond capacity to simulate an interrupt
        // producer outrunning the foreground consumer.
        for ticks in 0..crate::irq_rx::RX_QUEUE_CAPACITY as u32 {
            assert!(queue.push(RxEvent::Byte {
                value: 0x55,
                ticks: ticks * 100,
            }));
        }

        // The extra byte is lost and marks the queue as overflowed.
        assert!(!queue.push(RxEvent::Byte {
            value: 0x55,
            ticks: 10_000,
        }));

        // Recovery discards all queued bytes and begins silence timing.
        drain_events(&mut queue, &mut receiver, 10_000);

        assert!(!receiver.frames().has_ready_frame());

        // No valid frame can appear before synchronization returns.
        drain_events(&mut queue, &mut receiver, 10_000 + T3_5_US - 1);

        assert!(!receiver.frames().has_ready_frame());

        // After t3.5 silence, receiving can resume.
        drain_events(&mut queue, &mut receiver, 10_000 + T3_5_US);

        queue_frame(&mut queue, 10_000 + T3_5_US + 100);

        drain_events(&mut queue, &mut receiver, 10_000 + T3_5_US + 400 + T3_5_US);

        assert_eq!(
            receiver.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }

    #[test]
    fn atomic_read_prioritizes_overflow() {
        let mut queue = RxQueue::new();

        // Fill the queue with events.
        for ticks in 0..RX_QUEUE_CAPACITY as u32 {
            assert!(queue.push(RxEvent::Byte { value: 0x55, ticks }));
        }

        // The next event causes an overflow.
        assert!(!queue.push(RxEvent::Error { ticks: 1000 }));

        // The consumer must observe overflow before any queued byte.
        assert!(matches!(queue.read_next(), RxQueueRead::Overflow));

        // Overflow recovery must leave the queue empty.
        assert!(matches!(queue.read_next(), RxQueueRead::Empty));
    }

    #[test]
    fn overflow_after_partial_drain_discards_incomplete_frame() {
        let mut queue = RxQueue::new();
        let mut receiver = RtuReceiver::new();

        // Simulate the foreground worker consuming the beginning
        // of a Modbus RTU request.
        queue_byte(&mut queue, 0x11, 100);
        queue_byte(&mut queue, 0x03, 200);

        // Process the first byte using its interrupt timestamp.
        match queue.read_next() {
            RxQueueRead::Event(RxEvent::Byte { value, ticks }) => {
                receiver.on_byte(value, ticks);
            }
            _ => panic!("expected first received byte"),
        }

        // The interrupt producer now outruns the consumer.
        // Fill the remaining queue slots.
        for ticks in 0..(RX_QUEUE_CAPACITY - 1) as u32 {
            assert!(queue.push(RxEvent::Byte {
                value: 0x55,
                ticks: 300 + ticks * 100,
            }));
        }

        // One additional event causes overflow and latches the fault.
        assert!(!queue.push(RxEvent::Error { ticks: 10_000 }));

        // The next queue read must report overflow before returning
        // any more bytes from the damaged transmission.
        assert!(matches!(queue.read_next(), RxQueueRead::Overflow));

        // Notify the RTU receiver that synchronization was lost.
        receiver.on_stream_loss(10_000);

        // Even after sufficient silence, the incomplete frame
        // must never become available for Modbus processing.
        receiver.poll(10_000 + T3_5_US);

        assert!(!receiver.frames().has_ready_frame());
    }
}

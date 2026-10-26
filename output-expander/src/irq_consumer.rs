//! Foreground processing of timestamped UART receive events.
//!
//! The interrupt handler queues received bytes and UART errors.
//! The foreground worker transfers those events into the Modbus RTU
//! receiver and checks whether a frame has completed.

use crate::irq_rx::{RxEvent, RxQueue, RxQueueRead};
use crate::rtu_receiver::RtuReceiver;

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
}

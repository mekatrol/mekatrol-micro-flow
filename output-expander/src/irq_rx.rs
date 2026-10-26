//! Fixed-capacity USART1 interrupt receive queue.
//!
//! The interrupt produces timestamped UART events.
//! The main loop consumes them and performs Modbus RTU framing.
//!
//! This module does not access UART registers or enable interrupts.

use core::cell::RefCell;

use critical_section::Mutex;

/// Number of events the queue can hold.
///
/// Must be large enough to accommodate a complete Modbus request
/// plus any events received before the main loop drains the queue.
pub const RX_QUEUE_CAPACITY: usize = 32;

/// One event captured by the USART1 interrupt.
#[derive(Clone, Copy)]
pub enum RxEvent {
    /// A successfully received UART byte and its SysTick timestamp.
    Byte { value: u8, ticks: u32 },

    /// UART parity, framing, noise, or overrun error.
    Error,
}

/// Fixed-size FIFO for interrupt-to-foreground communication.
pub struct RxQueue {
    events: [Option<RxEvent>; RX_QUEUE_CAPACITY],
    head: usize,
    tail: usize,
    len: usize,
    overflowed: bool,
}

impl RxQueue {
    /// Construct an empty queue without allocation.
    pub const fn new() -> Self {
        Self {
            events: [None; RX_QUEUE_CAPACITY],
            head: 0,
            tail: 0,
            len: 0,
            overflowed: false,
        }
    }

    /// Insert one event, returning false if the queue is full.
    pub fn push(&mut self, event: RxEvent) -> bool {
        if self.len == RX_QUEUE_CAPACITY {
            self.overflowed = true;
            return false;
        }

        self.events[self.head] = Some(event);
        self.head = (self.head + 1) % RX_QUEUE_CAPACITY;
        self.len += 1;
        true
    }

    /// Remove the oldest event.
    pub fn pop(&mut self) -> Option<RxEvent> {
        if self.len == 0 {
            return None;
        }

        let event = self.events[self.tail].take();
        self.tail = (self.tail + 1) % RX_QUEUE_CAPACITY;
        self.len -= 1;
        event
    }

    /// Report and clear a queue overflow.
    ///
    /// An overflow invalidates the current Modbus RTU frame
    /// because at least one UART event was lost.
    pub fn take_overflow(&mut self) -> bool {
        let overflowed = self.overflowed;
        self.overflowed = false;
        overflowed
    }
}

/// Shared queue protected by short critical sections.
///
/// The interrupt and main loop must never access the underlying
/// queue without acquiring this mutex.
static RX_QUEUE: Mutex<RefCell<RxQueue>> = Mutex::new(RefCell::new(RxQueue::new()));

/// Push an event from the USART1 interrupt.
///
/// Keep the critical section short: no parsing, logging,
/// delays, or UART transmission occurs here.
pub fn push_from_interrupt(event: RxEvent) {
    critical_section::with(|cs| {
        RX_QUEUE.borrow(cs).borrow_mut().push(event);
    });
}

/// Pop one event from the foreground loop.
pub fn pop_from_main() -> Option<RxEvent> {
    critical_section::with(|cs| RX_QUEUE.borrow(cs).borrow_mut().pop())
}

/// Check whether the queue has overflowed.
pub fn take_overflow() -> bool {
    critical_section::with(|cs| RX_QUEUE.borrow(cs).borrow_mut().take_overflow())
}

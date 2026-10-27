use ch32_hal::gpio::Output;

/// Controls the RS-485 transceiver direction.
///
/// The PCB connects DE and /RE together to PC6.
///
pub struct Rs485Direction<'d> {
    /// Driver enable, connected to PC6.
    de: Output<'d>,
}

impl<'d> Rs485Direction<'d> {
    /// Construct a direction controller.
    ///
    /// IMPORTANT:
    /// Both GPIO outputs must already be initialized LOW
    /// before calling this function.
    ///
    /// This prevents the device from accidentally driving
    /// the RS-485 bus during startup.
    pub fn new(de: Output<'d>) -> Self {
        Self { de }
    }

    /// Enable reception and disable transmission.
    ///
    /// DE LOW disables the RS-485 line driver.
    /// /RE LOW enables the receiver.
    ///
    /// On the production PCB, PC6 controls both signals.
    pub fn receive(&mut self) {
        // Release the shared bus before enabling reception.
        self.de.set_low();
    }

    /// Enable transmission and disable reception.
    ///
    /// DE HIGH enables the RS-485 line driver.
    /// /RE HIGH disables the receiver.
    ///
    /// WHY:
    /// Disabling reception during transmission avoids
    /// processing our own transmitted bytes as incoming data.
    pub fn transmit(&mut self) {
        // The shared signal disables /RE while it enables DE.
        self.de.set_high();
    }
}

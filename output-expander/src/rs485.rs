use ch32_hal::gpio::Output;

/// Controls the RS-485 transceiver direction.
///
/// The production PCB connects DE and /RE together to PC6.
/// The evaluation board provides independent PC6 and PC7 pins.
///
/// WHY:
/// Using Option<Output<'d>> allows the same implementation
/// to support both boards without runtime board detection
/// or duplicated communication code.
pub struct Rs485Direction<'d> {
    /// Driver enable, connected to PC6.
    de: Output<'d>,

    /// Optional receiver-enable control, connected to PC7.
    ///
    /// None means DE and /RE are electrically tied together.
    re: Option<Output<'d>>,
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
    pub fn new(de: Output<'d>, re: Option<Output<'d>>) -> Self {
        Self { de, re }
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

        // Evaluation board only:
        // /RE LOW enables the receiver.
        if let Some(re) = self.re.as_mut() {
            re.set_low();
        }
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
        // Disable the receiver first on the evaluation board.
        if let Some(re) = self.re.as_mut() {
            re.set_high();
        }

        // Enable the transmitter after receiver configuration.
        self.de.set_high();
    }
}

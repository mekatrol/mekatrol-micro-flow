use crate::{config::Config, outputs::Outputs, uid};

pub const HOLDING_COUNT: u16 = 5;

pub const INPUT_COUNT: u16 = 3 + uid::UID_REGISTERS as u16;

pub struct RegisterBank {
    pub config: Config,
    pub outputs: Outputs,
    pub status: u16,
    pub uid: [u8; uid::UID_BYTES],
}

impl RegisterBank {
    pub fn holding(&self, address: u16) -> Option<u16> {
        Some(match address {
            0 => self.config.slave_address as u16,
            1 => self.config.startup_outputs as u16,
            2 => self.config.comms_timeout_ms,
            3 => self.config.timeout_outputs as u16,
            4 => self.config.brightness as u16,
            _ => return None,
        })
    }

    pub fn input(&self, address: u16) -> Option<u16> {
        if address == 0 {
            Some((0u16 << 8) | 1)
        } else if address == 1 {
            Some(self.outputs.mask() as u16)
        } else if address == 2 {
            Some(self.status)
        } else {
            uid::to_registers(self.uid)
                .get((address - 3) as usize)
                .copied()
        }
    }
}

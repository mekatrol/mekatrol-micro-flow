#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub enum Status {
    Unconfigured,
    Healthy,
    Command,
    CommsTimeout,
    FlashFault,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub struct Rgb(pub u8, pub u8, pub u8);

pub const fn color(status: Status, brightness: u8) -> Rgb {

    match status {
        Status::Unconfigured => Rgb(brightness, brightness / 3, 0),
        Status::Healthy => Rgb(0, brightness, 0),
        Status::Command => Rgb(0, 0, brightness),
        Status::CommsTimeout | Status::FlashFault => Rgb(brightness, 0, 0),
    }
}

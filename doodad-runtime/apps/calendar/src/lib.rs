#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec, request_calendar};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const AGENDA: &[u8] = include_bytes!("../screens/agenda.cbor");
const DETAIL: &[u8] = include_bytes!("../screens/detail.cbor");
const CONFIRMED: &[u8] = include_bytes!("../screens/confirmed.cbor");
const TRAVEL: &[u8] = include_bytes!("../screens/travel.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "calendar.open.detail" => Some(DETAIL),
        "calendar.travel" => Some(TRAVEL),
        "calendar.rsvp.yes" => Some(CONFIRMED),
        "calendar.back" => Some(AGENDA),
        "calendar.reconnect" => Some(CONFIRMED),
        _ => None,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(HOME);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(
    pointer: *const u8,
    length: u32,
) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    let bytes = unsafe {
        core::slice::from_raw_parts(pointer, length as usize)
    };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let target = match target_for(event.action_id) {
        Some(value) => value,
        None => return 0,
    };
    // Each package imports only its domain-scoped mocked capability.
    if request_calendar(event.action_id, &[]).is_err() {
        return 0;
    }
    let _ = mount_appspec(target);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

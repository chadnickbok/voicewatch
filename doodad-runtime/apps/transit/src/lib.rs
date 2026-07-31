#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec, request_transit};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const DEPARTURES: &[u8] = include_bytes!("../screens/departures.cbor");
const STALE: &[u8] = include_bytes!("../screens/stale.cbor");
const ALERT: &[u8] = include_bytes!("../screens/alert.cbor");
const RECOVERED: &[u8] = include_bytes!("../screens/recovered.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "transit.primary" => Some(DEPARTURES),
        "transit.offline" => Some(STALE),
        "transit.alert" => Some(ALERT),
        "transit.reconnect" => Some(RECOVERED),
        "transit.cache" => Some(STALE),
        "transit.departures" => Some(DEPARTURES),
        "transit.refresh" => Some(DEPARTURES),
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
    if request_transit(event.action_id, &[]).is_err() {
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

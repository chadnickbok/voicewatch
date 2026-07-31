#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec, request_navigation};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const MANEUVER: &[u8] = include_bytes!("../screens/maneuver.cbor");
const OVERVIEW: &[u8] = include_bytes!("../screens/overview.cbor");
const CACHED: &[u8] = include_bytes!("../screens/cached.cbor");
const RECOVERED: &[u8] = include_bytes!("../screens/recovered.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "navigation.primary" => Some(MANEUVER),
        "navigation.lose" => Some(CACHED),
        "navigation.overview" => Some(OVERVIEW),
        "navigation.continue" => Some(MANEUVER),
        "navigation.recover" => Some(RECOVERED),
        "navigation.next" => Some(MANEUVER),
        "navigation.home" => Some(HOME),
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
    if request_navigation(event.action_id, &[]).is_err() {
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

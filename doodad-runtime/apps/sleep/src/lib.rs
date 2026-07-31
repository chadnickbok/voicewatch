#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec, request_sleep};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const OVERNIGHT: &[u8] = include_bytes!("../screens/overnight.cbor");
const SUMMARY: &[u8] = include_bytes!("../screens/summary.cbor");
const STAGES: &[u8] = include_bytes!("../screens/stages.cbor");
const HISTORY: &[u8] = include_bytes!("../screens/history.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "sleep.primary" => Some(OVERNIGHT),
        "sleep.morning" => Some(SUMMARY),
        "sleep.wake" => Some(SUMMARY),
        "sleep.stages" => Some(STAGES),
        "sleep.again" => Some(OVERNIGHT),
        "sleep.history" => Some(HISTORY),
        "sleep.summary" => Some(SUMMARY),
        "sleep.last" => Some(SUMMARY),
        "sleep.home" => Some(HOME),
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
    if request_sleep(event.action_id, &[]).is_err() {
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

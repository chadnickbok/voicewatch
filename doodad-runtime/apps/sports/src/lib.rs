#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec, request_sports};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const LIVE: &[u8] = include_bytes!("../screens/live.cbor");
const BURST: &[u8] = include_bytes!("../screens/burst.cbor");
const FINAL: &[u8] = include_bytes!("../screens/final.cbor");
const TIMELINE: &[u8] = include_bytes!("../screens/timeline.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "sports.primary" => Some(LIVE),
        "sports.burst" => Some(BURST),
        "sports.unfollow" => Some(FINAL),
        "sports.end" => Some(FINAL),
        "sports.live" => Some(LIVE),
        "sports.plays" => Some(TIMELINE),
        "sports.follow" => Some(LIVE),
        "sports.final" => Some(FINAL),
        "sports.home" => Some(HOME),
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
    if request_sports(event.action_id, &[]).is_err() {
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

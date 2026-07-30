#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::display_text;

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    display_text("Hello from Wasm");
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, UiCommandBuffer, decode_ui_event, mount_appspec, pack_result,
};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const QUICK_ADD: &[u8] = include_bytes!("../screens/quick-add.cbor");
const VOICE_REVIEW: &[u8] = include_bytes!("../screens/voice-review.cbor");

struct Runtime {
    total: u32,
    quick_amount: u32,
    text: [u8; 24],
    commands: UiCommandBuffer<256>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            total: 1420,
            quick_amount: 100,
            text: [0; 24],
            commands: UiCommandBuffer::new(),
        }
    }

    fn render_home(&mut self) -> u64 {
        let total = format_kcal(self.total, &mut self.text);
        if self.commands.begin(3).is_err()
            || self
                .commands
                .set_primary_text("today.total", total)
                .is_err()
            || self
                .commands
                .set_maximum("today.progress", 3000)
                .is_err()
            || self
                .commands
                .set_value("today.progress", i64::from(self.total))
                .is_err()
        {
            return 0;
        }
        match self.commands.finish() {
            Ok(commands) => pack_result(commands),
            Err(_) => 0,
        }
    }

    fn render_quick_amount(&mut self) -> u64 {
        if self.commands.begin(1).is_err()
            || self
                .commands
                .set_value(
                    "calories.quick.amount",
                    i64::from(self.quick_amount),
                )
                .is_err()
        {
            return 0;
        }
        match self.commands.finish() {
            Ok(commands) => pack_result(commands),
            Err(_) => 0,
        }
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime =
    SharedRuntime(UnsafeCell::new(Runtime::new()));

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
    let bytes =
        unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    match event.action_id {
        "open_quick_add" => {
            let _ = mount_appspec(QUICK_ADD);
            runtime.render_quick_amount()
        }
        "calories.amount.changed" => {
            if let EventValue::Integer(value) = event.value {
                if (50..=1000).contains(&value) {
                    runtime.quick_amount = value as u32;
                    return runtime.render_quick_amount();
                }
            }
            0
        }
        "calories.add" => {
            runtime.total =
                runtime.total.saturating_add(runtime.quick_amount);
            let _ = mount_appspec(HOME);
            runtime.render_home()
        }
        "calories.voice" => {
            let _ = mount_appspec(VOICE_REVIEW);
            0
        }
        "calories.confirm.voice" => {
            runtime.total = runtime.total.saturating_add(650);
            let _ = mount_appspec(HOME);
            runtime.render_home()
        }
        "calories.correct" => {
            runtime.quick_amount = 650;
            let _ = mount_appspec(QUICK_ADD);
            runtime.render_quick_amount()
        }
        _ => 0,
    }
}

fn format_kcal(value: u32, output: &mut [u8; 24]) -> &str {
    let mut reverse = [0_u8; 10];
    let mut digits = 0;
    let mut remaining = value;
    loop {
        reverse[digits] = b'0' + (remaining % 10) as u8;
        digits += 1;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }
    let mut cursor = 0;
    while digits != 0 {
        digits -= 1;
        output[cursor] = reverse[digits];
        cursor += 1;
    }
    let suffix = b" kcal";
    output[cursor..cursor + suffix.len()].copy_from_slice(suffix);
    cursor += suffix.len();
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

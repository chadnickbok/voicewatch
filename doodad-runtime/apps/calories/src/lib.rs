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
    context: [u8; 24],
    commands: UiCommandBuffer<256>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            total: 1420,
            quick_amount: 100,
            text: [0; 24],
            context: [0; 24],
            commands: UiCommandBuffer::new(),
        }
    }

    fn render_home(&mut self) -> u64 {
        let total = format_kcal(self.total, &mut self.text);
        let context =
            format_context(self.total, &mut self.context);
        let progress_maximum = core::cmp::max(2000, self.total);
        if self.commands.begin(4).is_err()
            || self
                .commands
                .set_primary_text("today.context", context)
                .is_err()
            || self
                .commands
                .set_primary_text("today.total", total)
                .is_err()
            || self
                .commands
                .set_maximum(
                    "today.progress",
                    i64::from(progress_maximum),
                )
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
    let cursor = write_number(value, output, 0);
    let suffix = b" kcal";
    output[cursor..cursor + suffix.len()].copy_from_slice(suffix);
    let end = cursor + suffix.len();
    unsafe { core::str::from_utf8_unchecked(&output[..end]) }
}

fn format_context(value: u32, output: &mut [u8; 24]) -> &str {
    let prefix = b"TODAY / ";
    output[..prefix.len()].copy_from_slice(prefix);
    let remaining = value.abs_diff(2000);
    let cursor = write_number(remaining, output, prefix.len());
    let suffix = if value <= 2000 {
        b" LEFT".as_slice()
    } else {
        b" OVER".as_slice()
    };
    output[cursor..cursor + suffix.len()].copy_from_slice(suffix);
    let end = cursor + suffix.len();
    unsafe { core::str::from_utf8_unchecked(&output[..end]) }
}

fn write_number(
    value: u32,
    output: &mut [u8],
    mut cursor: usize,
) -> usize {
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
    let digit_count = digits;
    while digits != 0 {
        digits -= 1;
        if digits != digit_count - 1 && (digits + 1) % 3 == 0 {
            output[cursor] = b',';
            cursor += 1;
        }
        output[cursor] = reverse[digits];
        cursor += 1;
    }
    cursor
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

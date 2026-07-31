#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    Freshness, UiCommandBuffer, decode_provider_event,
    decode_ui_event, decode_weather_provider_payload, mount_appspec,
    pack_result, request_provider,
};

const APPSPEC: &[u8] = include_bytes!("../appspec.cbor");

struct Runtime {
    temperature: [u8; 16],
    status: [u8; 64],
    commands: UiCommandBuffer<512>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            temperature: [0; 16],
            status: [0; 64],
            commands: UiCommandBuffer::new(),
        }
    }

    fn loading(&mut self) -> u64 {
        if self.commands.begin(3).is_err()
            || self
                .commands
                .set_primary_text("weather.status", "Updating…")
                .is_err()
            || self
                .commands
                .set_primary_text("weather.primary", "Waiting…")
                .is_err()
            || self
                .commands
                .set_enabled("weather.primary", false)
                .is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }

    fn snapshot(
        &mut self,
        freshness: Freshness,
        temperature_tenths: i32,
        condition: &str,
        detail: &str,
        data_revision: u64,
        age_minutes: u64,
    ) -> u64 {
        let temperature =
            format_temperature(temperature_tenths, &mut self.temperature);
        let status = format_status(
            freshness,
            data_revision,
            age_minutes,
            &mut self.status,
        );
        if self.commands.begin(6).is_err()
            || self
                .commands
                .set_primary_text("weather.summary", temperature)
                .is_err()
            || self
                .commands
                .set_primary_text("weather.forecast", condition)
                .is_err()
            || self
                .commands
                .set_secondary_text("weather.forecast", detail)
                .is_err()
            || self
                .commands
                .set_primary_text("weather.status", status)
                .is_err()
            || self
                .commands
                .set_primary_text("weather.primary", "Refresh")
                .is_err()
            || self
                .commands
                .set_enabled("weather.primary", true)
                .is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime =
    SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(APPSPEC);
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
    if event.action_id != "weather.refresh"
        || request_provider("weather", "refresh", &[]).is_err()
    {
        return 0;
    }
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    runtime.loading()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_provider_event(
    pointer: *const u8,
    length: u32,
) -> u64 {
    if pointer.is_null() || length == 0 || length > 1024 {
        return 0;
    }
    let bytes =
        unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_provider_event(bytes) {
        Ok(value)
            if value.provider_id == "weather"
                && value.event_id == "weather.snapshot" =>
        {
            value
        }
        _ => return 0,
    };
    let weather = match decode_weather_provider_payload(event.payload) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    runtime.snapshot(
        event.freshness,
        weather.temperature_tenths_f,
        weather.condition,
        weather.detail,
        weather.data_revision,
        weather.cache_age_minutes,
    )
}

fn finish(commands: &UiCommandBuffer<512>) -> u64 {
    match commands.finish() {
        Ok(bytes) => pack_result(bytes),
        Err(_) => 0,
    }
}

fn format_temperature(value: i32, output: &mut [u8; 16]) -> &str {
    let rounded = if value >= 0 {
        (value + 5) / 10
    } else {
        (value - 5) / 10
    };
    let mut cursor = 0;
    if rounded < 0 {
        output[cursor] = b'-';
        cursor += 1;
    }
    cursor = write_unsigned(rounded.unsigned_abs() as u64, output, cursor);
    output[cursor] = 0xc2;
    output[cursor + 1] = 0xb0;
    cursor += 2;
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn format_status(
    freshness: Freshness,
    revision: u64,
    age: u64,
    output: &mut [u8; 64],
) -> &str {
    let (prefix, number, suffix) = match freshness {
        Freshness::Current => ("Updated now · revision ", revision, ""),
        Freshness::Stale => ("Cached · ", age, " min old"),
        Freshness::Offline => ("Offline · cache ", age, " min"),
        Freshness::Error => ("Weather error · revision ", revision, ""),
    };
    let mut cursor = copy(prefix.as_bytes(), output, 0);
    cursor = write_unsigned(number, output, cursor);
    cursor = copy(suffix.as_bytes(), output, cursor);
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

fn copy(source: &[u8], output: &mut [u8], offset: usize) -> usize {
    let end = offset + source.len();
    output[offset..end].copy_from_slice(source);
    end
}

fn write_unsigned(
    value: u64,
    output: &mut [u8],
    offset: usize,
) -> usize {
    let mut reverse = [0_u8; 20];
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
    let mut cursor = offset;
    while digits > 0 {
        digits -= 1;
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

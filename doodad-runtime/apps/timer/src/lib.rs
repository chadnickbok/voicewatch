#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, TimerProviderState, UiCommandBuffer, acknowledge_timer,
    cancel_timer, decode_provider_event, decode_timer_provider_payload,
    decode_ui_event, mount_appspec, pack_result, schedule_timer_after,
};

const APPSPEC: &[u8] = include_bytes!("../appspec.cbor");
const TIMER_ID: &str = "timer.primary";

#[derive(Clone, Copy, Eq, PartialEq)]
enum Mode {
    Ready,
    Scheduled,
    Firing,
}

struct Runtime {
    minutes: u32,
    mode: Mode,
    text: [u8; 24],
    commands: UiCommandBuffer<256>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            minutes: 1,
            mode: Mode::Ready,
            text: [0; 24],
            commands: UiCommandBuffer::new(),
        }
    }

    fn render_state(
        &mut self,
        remaining_ms: u64,
    ) -> u64 {
        let text = match self.mode {
            Mode::Firing => "0:00",
            Mode::Ready => format_duration(
                u64::from(self.minutes) * 60_000,
                &mut self.text,
            ),
            Mode::Scheduled => {
                format_duration(remaining_ms, &mut self.text)
            }
        };
        let action = match self.mode {
            Mode::Ready => "Start",
            Mode::Scheduled => "Cancel",
            Mode::Firing => "Dismiss",
        };
        let remaining_seconds =
            remaining_ms.saturating_add(999) / 1_000;
        let duration_seconds =
            u64::from(self.minutes) * 60;
        if self.commands.begin(7).is_err()
            || self
                .commands
                .set_primary_text("timer.summary", text)
                .is_err()
            || self
                .commands
                .set_maximum(
                    "timer.progress",
                    duration_seconds.min(i64::MAX as u64) as i64,
                )
                .is_err()
            || self
                .commands
                .set_value(
                    "timer.progress",
                    remaining_seconds.min(i64::MAX as u64) as i64,
                )
                .is_err()
            || self
                .commands
                .set_value("timer.duration", i64::from(self.minutes))
                .is_err()
            || self
                .commands
                .set_primary_text("timer.primary", action)
                .is_err()
            || self
                .commands
                .set_enabled(
                    "timer.duration",
                    self.mode == Mode::Ready,
                )
                .is_err()
            || self
                .commands
                .set_visible(
                    "timer.duration",
                    self.mode == Mode::Ready,
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

// Doodad invokes one package as a serialized actor.
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
    let runtime = unsafe { &mut *RUNTIME.0.get() };

    if event.action_id == "timer.duration.changed" {
        if runtime.mode != Mode::Ready {
            return 0;
        }
        if let EventValue::Integer(value) = event.value {
            if (1..=60).contains(&value) {
                runtime.minutes = value as u32;
                return runtime.render_state(
                    u64::from(runtime.minutes) * 60_000,
                );
            }
        }
        return 0;
    }
    if event.action_id != TIMER_ID {
        return 0;
    }

    match runtime.mode {
        Mode::Ready => {
            let duration = runtime.minutes.saturating_mul(60_000);
            if schedule_timer_after(TIMER_ID, duration).is_err() {
                return 0;
            }
            runtime.mode = Mode::Scheduled;
            runtime.render_state(u64::from(duration))
        }
        Mode::Scheduled => {
            if cancel_timer(TIMER_ID).is_err() {
                return 0;
            }
            runtime.mode = Mode::Ready;
            runtime.render_state(
                u64::from(runtime.minutes) * 60_000,
            )
        }
        Mode::Firing => {
            if acknowledge_timer(TIMER_ID).is_err() {
                return 0;
            }
            runtime.mode = Mode::Ready;
            runtime.render_state(
                u64::from(runtime.minutes) * 60_000,
            )
        }
    }
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
            if value.provider_id == "exact_scheduler"
                && value.event_id == "timer.changed" =>
        {
            value
        }
        _ => return 0,
    };
    let timer = match decode_timer_provider_payload(event.payload) {
        Ok(value) if value.id == TIMER_ID => value,
        _ => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    runtime.mode = match timer.state {
        TimerProviderState::Scheduled => Mode::Scheduled,
        TimerProviderState::Firing => Mode::Firing,
        TimerProviderState::Acknowledged
        | TimerProviderState::Cancelled => Mode::Ready,
    };
    runtime.render_state(timer.remaining_ms)
}

fn format_duration(milliseconds: u64, output: &mut [u8; 24]) -> &str {
    let total_seconds = milliseconds.saturating_add(999) / 1_000;
    let minutes = total_seconds / 60;
    let seconds = total_seconds % 60;
    let mut cursor = write_unsigned(minutes, output, 0);
    output[cursor] = b':';
    cursor += 1;
    output[cursor] = b'0' + (seconds / 10) as u8;
    output[cursor + 1] = b'0' + (seconds % 10) as u8;
    cursor += 2;
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
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

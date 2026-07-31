#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, UiCommandBuffer, decode_ui_event, mount_appspec, pack_result,
};

const APPSPEC: &[u8] = include_bytes!("../appspec.cbor");
const SCALE: i64 = 1_000;
const LIMIT: i64 = 999_999_999_999 * SCALE;

struct Calculator {
    entry: i64,
    accumulator: i64,
    pending: u8,
    entering: bool,
    decimal: bool,
    fractional_digits: u8,
    evaluated: bool,
    error: bool,
}

impl Calculator {
    const fn new() -> Self {
        Self {
            entry: 0,
            accumulator: 0,
            pending: 0,
            entering: false,
            decimal: false,
            fractional_digits: 0,
            evaluated: false,
            error: false,
        }
    }

    fn clear(&mut self) {
        *self = Self::new();
    }

    fn key(&mut self, key: &str) {
        if key == "C" {
            self.clear();
            return;
        }
        if self.error {
            self.clear();
        }
        if key.len() == 1 {
            let byte = key.as_bytes()[0];
            if byte.is_ascii_digit() {
                self.digit(byte - b'0');
                return;
            }
        }
        match key {
            "." => {
                if self.evaluated {
                    self.clear();
                }
                self.entering = true;
                self.decimal = true;
                self.evaluated = false;
            }
            "+/-" => {
                self.entry = -self.entry;
                self.evaluated = false;
            }
            "%" => {
                self.entry /= 100;
                self.evaluated = false;
            }
            "<-" => self.backspace(),
            "+" | "-" | "*" | "/" => self.operator(key.as_bytes()[0]),
            "=" => self.equals(),
            _ => {}
        }
    }

    fn digit(&mut self, digit: u8) {
        if self.evaluated {
            self.clear();
        }
        if !self.entering {
            self.entry = 0;
            self.entering = true;
            self.decimal = false;
            self.fractional_digits = 0;
        }
        if self.decimal {
            if self.fractional_digits >= 3 {
                return;
            }
            let place = match self.fractional_digits {
                0 => 100,
                1 => 10,
                _ => 1,
            };
            self.entry = self
                .entry
                .saturating_add(i64::from(digit) * place);
            self.fractional_digits += 1;
        } else {
            let negative = self.entry < 0;
            let whole = self.entry.abs() / SCALE;
            let next = whole
                .saturating_mul(10)
                .saturating_add(i64::from(digit))
                .saturating_mul(SCALE)
                .min(LIMIT);
            self.entry = if negative { -next } else { next };
        }
    }

    fn backspace(&mut self) {
        if !self.entering || self.evaluated {
            self.clear();
            return;
        }
        if self.decimal && self.fractional_digits > 0 {
            let place = match self.fractional_digits {
                1 => 100,
                2 => 10,
                _ => 1,
            };
            let sign = if self.entry < 0 { -1 } else { 1 };
            let absolute = self.entry.abs();
            self.entry = sign * (absolute - (absolute / place % 10) * place);
            self.fractional_digits -= 1;
        } else if self.decimal {
            self.decimal = false;
        } else {
            self.entry = (self.entry / SCALE / 10) * SCALE;
        }
    }

    fn operator(&mut self, operator: u8) {
        if self.pending != 0 && self.entering {
            self.apply_pending();
        } else if self.pending == 0 {
            self.accumulator = self.entry;
        }
        if !self.error {
            self.pending = operator;
            self.entering = false;
            self.decimal = false;
            self.fractional_digits = 0;
            self.evaluated = false;
        }
    }

    fn equals(&mut self) {
        if self.pending != 0 && self.entering {
            self.apply_pending();
        }
        self.pending = 0;
        self.entering = false;
        self.evaluated = true;
    }

    fn apply_pending(&mut self) {
        let value = match self.pending {
            b'+' => self.accumulator.checked_add(self.entry),
            b'-' => self.accumulator.checked_sub(self.entry),
            b'*' => self
                .accumulator
                .checked_mul(self.entry)
                .map(|value| value / SCALE),
            b'/' if self.entry != 0 => self
                .accumulator
                .checked_mul(SCALE)
                .map(|value| value / self.entry),
            _ => None,
        };
        match value {
            Some(value) if value.abs() <= LIMIT => {
                self.accumulator = value;
                self.entry = value;
            }
            _ => {
                self.entry = 0;
                self.accumulator = 0;
                self.error = true;
            }
        }
    }
}

struct Runtime {
    calculator: Calculator,
    display: [u8; 32],
    commands: UiCommandBuffer<128>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            calculator: Calculator::new(),
            display: [0; 32],
            commands: UiCommandBuffer::new(),
        }
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);

// Doodad invokes each guest as one serialized actor.
unsafe impl Sync for SharedRuntime {}

static RUNTIME: SharedRuntime = SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(APPSPEC);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(pointer: *const u8, length: u32) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    let bytes = unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_ui_event(bytes) {
        Ok(event) => event,
        Err(_) => return 0,
    };
    if event.action_id != "key_pressed" {
        return 0;
    }
    let key = match event.value {
        EventValue::Text(value) => value,
        _ => return 0,
    };

    // SAFETY: the host serializes app events and never calls a guest
    // concurrently.
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    runtime.calculator.key(key);
    let text = if runtime.calculator.error {
        "Error"
    } else {
        format_milli(runtime.calculator.entry, &mut runtime.display)
    };
    if runtime.commands.begin(1).is_err()
        || runtime
            .commands
            .set_primary_text("calculator.result", text)
            .is_err()
    {
        return 0;
    }
    match runtime.commands.finish() {
        Ok(commands) => pack_result(commands),
        Err(_) => 0,
    }
}

fn format_milli(value: i64, output: &mut [u8; 32]) -> &str {
    let negative = value < 0;
    let absolute = value.abs();
    let whole = absolute / SCALE;
    let fraction = absolute % SCALE;
    let mut cursor = 0;
    if negative {
        output[cursor] = b'-';
        cursor += 1;
    }

    let mut reversed = [0_u8; 20];
    let mut digits = 0;
    let mut remaining = whole;
    loop {
        reversed[digits] = b'0' + (remaining % 10) as u8;
        digits += 1;
        remaining /= 10;
        if remaining == 0 {
            break;
        }
    }
    while digits > 0 {
        digits -= 1;
        output[cursor] = reversed[digits];
        cursor += 1;
    }

    if fraction != 0 {
        output[cursor] = b'.';
        cursor += 1;
        output[cursor] = b'0' + (fraction / 100) as u8;
        output[cursor + 1] = b'0' + (fraction / 10 % 10) as u8;
        output[cursor + 2] = b'0' + (fraction % 10) as u8;
        cursor += 3;
        while output[cursor - 1] == b'0' {
            cursor -= 1;
        }
    }

    // SAFETY: the function writes ASCII digits, sign, and decimal point only.
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

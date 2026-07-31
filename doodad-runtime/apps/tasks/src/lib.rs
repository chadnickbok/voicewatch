#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    UiCommandBuffer, decode_ui_event, mount_appspec, pack_result,
};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const LIST: &[u8] = include_bytes!("../screens/list.cbor");
const LIST_THREE: &[u8] = include_bytes!("../screens/list-three.cbor");

struct Runtime {
    milk_done: bool,
    coffee_done: bool,
    bananas: bool,
    bananas_done: bool,
    heading: [u8; 32],
    summary: [u8; 32],
    commands: UiCommandBuffer<512>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            milk_done: false,
            coffee_done: false,
            bananas: false,
            bananas_done: false,
            heading: [0; 32],
            summary: [0; 32],
            commands: UiCommandBuffer::new(),
        }
    }

    fn remaining(&self) -> u8 {
        u8::from(!self.milk_done)
            + u8::from(!self.coffee_done)
            + if self.bananas {
                u8::from(!self.bananas_done)
            } else {
                0
            }
    }

    fn render_list(&mut self) -> u64 {
        let remaining = self.remaining();
        let heading = count_text(
            "GROCERIES · ",
            remaining,
            " LEFT",
            &mut self.heading,
        );
        let command_count = if self.bananas { 4 } else { 3 };
        if self.commands.begin(command_count).is_err()
            || self
                .commands
                .set_primary_text("tasks.list.heading", heading)
                .is_err()
            || self
                .commands
                .set_primary_text(
                    "tasks.milk",
                    if self.milk_done {
                        "✓  Milk · undo"
                    } else {
                        "○  Milk"
                    },
                )
                .is_err()
            || self
                .commands
                .set_primary_text(
                    "tasks.coffee",
                    if self.coffee_done {
                        "✓  Coffee · undo"
                    } else {
                        "○  Coffee"
                    },
                )
                .is_err()
        {
            return 0;
        }
        if self.bananas
            && self
                .commands
                .set_primary_text(
                    "tasks.bananas",
                    if self.bananas_done {
                        "✓  Bananas · undo"
                    } else {
                        "○  Bananas"
                    },
                )
                .is_err()
        {
            return 0;
        }
        finish(&self.commands)
    }

    fn render_home(&mut self) -> u64 {
        let remaining = self.remaining();
        let summary = count_text(
            "Groceries · ",
            remaining,
            " left",
            &mut self.summary,
        );
        if self.commands.begin(1).is_err()
            || self
                .commands
                .set_primary_text("tasks.summary", summary)
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
        "tasks.open" => {
            let _ = mount_appspec(
                if runtime.bananas { LIST_THREE } else { LIST },
            );
            runtime.render_list()
        }
        "tasks.add.bananas" => {
            runtime.bananas = true;
            let _ = mount_appspec(LIST_THREE);
            runtime.render_list()
        }
        "tasks.toggle.milk" => {
            runtime.milk_done = !runtime.milk_done;
            runtime.render_list()
        }
        "tasks.toggle.coffee" => {
            runtime.coffee_done = !runtime.coffee_done;
            runtime.render_list()
        }
        "tasks.toggle.bananas" if runtime.bananas => {
            runtime.bananas_done = !runtime.bananas_done;
            runtime.render_list()
        }
        "tasks.back" => {
            let _ = mount_appspec(HOME);
            runtime.render_home()
        }
        _ => 0,
    }
}

fn finish(commands: &UiCommandBuffer<512>) -> u64 {
    match commands.finish() {
        Ok(bytes) => pack_result(bytes),
        Err(_) => 0,
    }
}

fn count_text<'a>(
    prefix: &str,
    count: u8,
    suffix: &str,
    output: &'a mut [u8],
) -> &'a str {
    let mut cursor = 0;
    output[..prefix.len()].copy_from_slice(prefix.as_bytes());
    cursor += prefix.len();
    output[cursor] = b'0' + count;
    cursor += 1;
    output[cursor..cursor + suffix.len()]
        .copy_from_slice(suffix.as_bytes());
    cursor += suffix.len();
    unsafe { core::str::from_utf8_unchecked(&output[..cursor]) }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

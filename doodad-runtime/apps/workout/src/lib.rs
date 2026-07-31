#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, UiCommandBuffer, decode_ui_event, mount_appspec, pack_result,
    request_workout,
};

const ACTIVE_SET: &[u8] = include_bytes!("../appspec.cbor");
const REST: &[u8] = include_bytes!("../screens/rest.cbor");
const NEXT_SET: &[u8] = include_bytes!("../screens/next-set.cbor");
const SUMMARY: &[u8] = include_bytes!("../screens/summary.cbor");

struct Runtime {
    weight: u32,
    commands: UiCommandBuffer<128>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            weight: 135,
            commands: UiCommandBuffer::new(),
        }
    }

    fn set_weight(&mut self, target: &str) -> u64 {
        if self.commands.begin(1).is_err()
            || self
                .commands
                .set_value(target, i64::from(self.weight))
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
    let _ = mount_appspec(ACTIVE_SET);
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
        "set_weight" | "workout.next.weight" => {
            if let EventValue::Integer(value) = event.value {
                if (45..=500).contains(&value) && value % 5 == 0 {
                    runtime.weight = value as u32;
                    let target = if event.action_id == "set_weight" {
                        "active_set.weight"
                    } else {
                        "workout.next.weight"
                    };
                    return runtime.set_weight(target);
                }
            }
            0
        }
        "complete_set" => {
            if request_workout("workout.complete", &[]).is_err() {
                return 0;
            }
            let _ = mount_appspec(REST);
            0
        }
        "workout.rest.finish" => {
            if request_workout("workout.rest.finish", &[]).is_err() {
                return 0;
            }
            let _ = mount_appspec(NEXT_SET);
            runtime.set_weight("workout.next.weight")
        }
        "workout.next.complete" | "workout.end" => {
            if request_workout("workout.commit", &[]).is_err() {
                return 0;
            }
            let _ = mount_appspec(SUMMARY);
            0
        }
        "workout.again" => {
            let _ = mount_appspec(ACTIVE_SET);
            runtime.set_weight("active_set.weight")
        }
        _ => 0,
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

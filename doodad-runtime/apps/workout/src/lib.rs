#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{
    EventValue, UiCommandBuffer, decode_ui_event, mount_appspec, pack_result, request_workout,
};

const TODAY: &[u8] = include_bytes!("../appspec.cbor");
const SESSION: &[u8] = include_bytes!("../screens/session.cbor");
const EXERCISE_PICKER: &[u8] = include_bytes!("../screens/exercise-picker.cbor");
const ACTIVE_SET: &[u8] = include_bytes!("../screens/active-set.cbor");
const WEIGHT_EDITOR: &[u8] = include_bytes!("../screens/weight-editor.cbor");
const SET_RESULT: &[u8] = include_bytes!("../screens/set-result.cbor");
const REST: &[u8] = include_bytes!("../screens/rest.cbor");
const PLATE_LOADING: &[u8] = include_bytes!("../screens/plate-loading.cbor");
const EXERCISE_SWITCHER: &[u8] = include_bytes!("../screens/exercise-switcher.cbor");
const MISSED_SET: &[u8] = include_bytes!("../screens/missed-set.cbor");
const SUMMARY: &[u8] = include_bytes!("../screens/summary.cbor");
const RESUME: &[u8] = include_bytes!("../screens/resume.cbor");

struct Runtime {
    weight: u32,
    reps: u32,
    rpe: u32,
    commands: UiCommandBuffer<256>,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            weight: 140,
            reps: 5,
            rpe: 8,
            commands: UiCommandBuffer::new(),
        }
    }

    fn set_value(&mut self, target: &str, value: u32) -> u64 {
        if self.commands.begin(1).is_err()
            || self.commands.set_value(target, i64::from(value)).is_err()
        {
            return 0;
        }
        match self.commands.finish() {
            Ok(commands) => pack_result(commands),
            Err(_) => 0,
        }
    }

    fn persist(&self, action: &str) -> bool {
        let payload = [self.weight as u8, self.reps as u8, self.rpe as u8];
        request_workout(action, &payload).is_ok()
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime = SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(TODAY);
}

fn mount(screen: &[u8]) -> u64 {
    let _ = mount_appspec(screen);
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(pointer: *const u8, length: u32) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    let bytes = unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let runtime = unsafe { &mut *RUNTIME.0.get() };
    match event.action_id {
        "workout.start" => mount(SESSION),
        "workout.resume.preview" => mount(RESUME),
        "workout.choose.exercise" => mount(EXERCISE_PICKER),
        "workout.exercise.back-squat"
        | "workout.exercise.front-squat"
        | "workout.exercise.paused-squat"
        | "workout.exercise.custom" => mount(SESSION),
        "workout.begin" => {
            if !runtime.persist("workout.start") {
                return 0;
            }
            mount(ACTIVE_SET)
        }
        "workout.edit.weight" => mount(WEIGHT_EDITOR),
        "workout.weight" => {
            if let EventValue::Integer(value) = event.value {
                if (20..=400).contains(&value) && value % 5 == 0 {
                    runtime.weight = value as u32;
                    if !runtime.persist("workout.adjust_weight") {
                        return 0;
                    }
                    return runtime.set_value("powerlifting.weight-editor.value", runtime.weight);
                }
            }
            0
        }
        "workout.weight.done" => mount(ACTIVE_SET),
        "workout.complete" | "workout.edit.result" => mount(SET_RESULT),
        "workout.reps" => {
            if let EventValue::Integer(value) = event.value {
                if (0..=20).contains(&value) {
                    runtime.reps = value as u32;
                    if !runtime.persist("workout.adjust_reps") {
                        return 0;
                    }
                    return runtime.set_value("powerlifting.set-result.reps", runtime.reps);
                }
            }
            0
        }
        "workout.rpe.7" | "workout.rpe.8" | "workout.rpe.9" | "workout.rpe.10" => {
            runtime.rpe = event
                .action_id
                .as_bytes()
                .last()
                .map_or(8, |digit| u32::from(*digit - b'0'));
            let _ = runtime.persist("workout.set_rpe");
            0
        }
        "workout.save" => {
            if !runtime.persist("workout.complete_set") {
                return 0;
            }
            if runtime.reps < 5 {
                mount(MISSED_SET)
            } else {
                mount(REST)
            }
        }
        "workout.missed.drop" => {
            runtime.weight = 135;
            let _ = runtime.persist("workout.drop_weight");
            0
        }
        "workout.missed.log" => {
            let _ = runtime.persist("workout.log_actual");
            0
        }
        "workout.missed.retry" => mount(ACTIVE_SET),
        "workout.missed.next" => mount(REST),
        "workout.rest.extend" => {
            let _ = runtime.persist("workout.extend_rest");
            0
        }
        "workout.rest.skip" => mount(ACTIVE_SET),
        "workout.plates" => mount(PLATE_LOADING),
        "workout.plates.ready" => mount(ACTIVE_SET),
        "workout.switch.preview" => mount(EXERCISE_SWITCHER),
        "workout.jump.squat" | "workout.jump.bench" | "workout.jump.deadlift" => {
            let _ = runtime.persist("workout.jump_exercise");
            mount(ACTIVE_SET)
        }
        "workout.finish" => {
            if !runtime.persist("workout.finish") {
                return 0;
            }
            mount(SUMMARY)
        }
        "workout.summary.done" | "workout.discard" => mount(TODAY),
        "workout.resume" => mount(ACTIVE_SET),
        _ => 0,
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

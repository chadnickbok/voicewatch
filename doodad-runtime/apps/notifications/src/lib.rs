#![no_std]

use core::cell::UnsafeCell;
use core::panic::PanicInfo;
use doodad_sdk::{decode_ui_event, mount_appspec};

const INBOX_TWO: &[u8] = include_bytes!("../appspec.cbor");
const INBOX_ONE: &[u8] = include_bytes!("../screens/inbox-one.cbor");
const INBOX_EMPTY: &[u8] = include_bytes!("../screens/inbox-empty.cbor");
const DETAIL: &[u8] = include_bytes!("../screens/detail.cbor");
const REPLY: &[u8] = include_bytes!("../screens/reply.cbor");
const REPLIED_ON_MY_WAY: &[u8] =
    include_bytes!("../screens/replied.cbor");
const REPLIED_SOUNDS_GOOD: &[u8] =
    include_bytes!("../screens/replied-sounds.cbor");

struct Runtime {
    maya_dismissed: bool,
    cleared: bool,
}

impl Runtime {
    const fn new() -> Self {
        Self {
            maya_dismissed: false,
            cleared: false,
        }
    }

    fn inbox(&self) -> &'static [u8] {
        if self.cleared {
            INBOX_EMPTY
        } else if self.maya_dismissed {
            INBOX_ONE
        } else {
            INBOX_TWO
        }
    }
}

struct SharedRuntime(UnsafeCell<Runtime>);
unsafe impl Sync for SharedRuntime {}
static RUNTIME: SharedRuntime =
    SharedRuntime(UnsafeCell::new(Runtime::new()));

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(INBOX_TWO);
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
    let destination = match event.action_id {
        "notification.open.maya" => DETAIL,
        "notification.reply" => REPLY,
        "notification.reply.back" => DETAIL,
        "notification.reply.on_my_way" => {
            runtime.maya_dismissed = true;
            REPLIED_ON_MY_WAY
        }
        "notification.reply.sounds_good" => {
            runtime.maya_dismissed = true;
            REPLIED_SOUNDS_GOOD
        }
        "notification.reply.done" => runtime.inbox(),
        "notification.dismiss" => {
            runtime.maya_dismissed = true;
            runtime.inbox()
        }
        "notification.back" => runtime.inbox(),
        "notification.clear" => {
            runtime.cleared = true;
            runtime.inbox()
        }
        "notification.open.build" => {
            runtime.cleared = true;
            INBOX_EMPTY
        }
        _ => return 0,
    };
    let _ = mount_appspec(destination);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

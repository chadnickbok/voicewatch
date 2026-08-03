#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{
    UiCommandBuffer, VoiceProviderState, decode_provider_event, decode_ui_event,
    decode_voice_provider_payload, mount_appspec, pack_result, request_audio,
};

const HOME: &[u8] = include_bytes!("../appspec.cbor");
const RECORDING: &[u8] = include_bytes!("../screens/recording.cbor");
const BUFFERED: &[u8] = include_bytes!("../screens/buffered.cbor");
const TRANSCRIPT: &[u8] = include_bytes!("../screens/transcript.cbor");
const SAVED: &[u8] = include_bytes!("../screens/saved.cbor");

fn target_for(action_id: &str) -> Option<&'static [u8]> {
    match action_id {
        "voice-notes.record" => Some(RECORDING),
        "voice-notes.finish-capture" => Some(BUFFERED),
        "voice-notes.pause" => Some(BUFFERED),
        "voice-notes.transcribe" => Some(TRANSCRIPT),
        "voice-notes.delete" => Some(HOME),
        "voice-notes.save" => Some(SAVED),
        "voice-notes.again" => Some(RECORDING),
        "voice-notes.open" => Some(TRANSCRIPT),
        "voice-notes.done" => Some(HOME),
        _ => None,
    }
}

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
    let bytes = unsafe {
        core::slice::from_raw_parts(pointer, length as usize)
    };
    let event = match decode_ui_event(bytes) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    let target = match target_for(event.action_id) {
        Some(value) => value,
        None => return 0,
    };
    // Each package imports only its domain-scoped mocked capability.
    if request_audio(event.action_id, &[]).is_err() {
        return 0;
    }
    let _ = mount_appspec(target);
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_provider_event(pointer: *const u8, length: u32) -> u64 {
    if pointer.is_null() || length == 0 || length > 1024 {
        return 0;
    }
    let bytes = unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    let event = match decode_provider_event(bytes) {
        Ok(value) if value.provider_id == "audio" && value.event_id == "voice.changed" => value,
        _ => return 0,
    };
    let voice = match decode_voice_provider_payload(event.payload) {
        Ok(value) => value,
        Err(_) => return 0,
    };
    match voice.state {
        VoiceProviderState::Recording => {
            let _ = mount_appspec(RECORDING);
            0
        }
        VoiceProviderState::Stopped => {
            let _ = mount_appspec(BUFFERED);
            0
        }
        VoiceProviderState::Transcript => {
            let _ = mount_appspec(TRANSCRIPT);
            let mut commands = UiCommandBuffer::<512>::new();
            if commands.begin(2).is_err()
                || commands
                    .set_secondary_text("voice-notes.transcript.detail", voice.text)
                    .is_err()
                || commands
                    .set_semantic_label("voice-notes.transcript.detail", voice.text)
                    .is_err()
            {
                return 0;
            }
            match commands.finish() {
                Ok(value) => pack_result(value),
                Err(_) => 0,
            }
        }
        VoiceProviderState::Error => {
            let _ = mount_appspec(HOME);
            0
        }
        VoiceProviderState::Connecting | VoiceProviderState::Ready => 0,
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

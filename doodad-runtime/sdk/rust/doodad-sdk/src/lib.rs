#![no_std]

/// Host ABI version implemented by this SDK.
pub const HOST_ABI_VERSION: u32 = 1;

#[link(wasm_import_module = "doodad")]
unsafe extern "C" {
    #[link_name = "display_text"]
    fn host_display_text(pointer: *const u8, length: u32);
}

/// Ask the trusted native shell to render a bounded UTF-8 string.
///
/// The bytes remain owned by the guest and are only borrowed for this call.
pub fn display_text(text: &str) {
    let length = u32::try_from(text.len()).expect("text does not fit the host ABI");

    // SAFETY: `text` is valid UTF-8 and its pointer remains valid for the
    // duration of this synchronous import call. The host independently
    // validates the pointer, length, UTF-8, and configured size bound.
    unsafe {
        host_display_text(text.as_ptr(), length);
    }
}

#![no_std]

/// Host ABI version implemented by this SDK.
pub const HOST_ABI_VERSION: u32 = 1;

#[link(wasm_import_module = "doodad")]
unsafe extern "C" {
    #[link_name = "ui_mount"]
    fn host_ui_mount(pointer: *const u8, length: u32) -> i32;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MountError {
    Empty,
    TooLarge,
    Rejected,
}

/// Mount a canonical semantic AppSpec document in the trusted native shell.
///
/// The bytes remain guest-owned only for this synchronous import. The host
/// validates the linear-memory range, immediately decodes into bounded
/// host-owned storage, enforces canonical CBOR and AppSpec quotas, and never
/// exposes an LVGL object or pointer to the guest.
pub fn mount_appspec(canonical_cbor: &[u8]) -> Result<(), MountError> {
    if canonical_cbor.is_empty() {
        return Err(MountError::Empty);
    }
    if canonical_cbor.len() > 4096 {
        return Err(MountError::TooLarge);
    }
    let length =
        u32::try_from(canonical_cbor.len()).map_err(|_| MountError::TooLarge)?;

    // SAFETY: the slice remains valid for this synchronous call. The host
    // independently validates its guest address and length before copying.
    let mounted = unsafe {
        host_ui_mount(canonical_cbor.as_ptr(), length)
    };
    if mounted == 1 {
        Ok(())
    } else {
        Err(MountError::Rejected)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum EventKind {
    Tap = 0,
    LongPress = 1,
    Repeat = 2,
    ValueChanging = 3,
    ValueCommitted = 4,
    CheckedChanged = 5,
    PageChanged = 6,
    Dismissed = 7,
    Submit = 8,
    Retry = 9,
    Cancel = 10,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UiEvent<'a> {
    pub app_id: &'a str,
    pub screen_id: &'a str,
    pub node_id: &'a str,
    pub action_id: &'a str,
    pub kind: EventKind,
    pub timestamp_monotonic_ms: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EventDecodeError {
    Truncated,
    NonCanonical,
    WrongType,
    WrongShape,
    InvalidUtf8,
    UnsupportedVersion,
    UnsupportedEvent,
    TrailingData,
}

struct CborReader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> CborReader<'a> {
    fn head(&mut self) -> Result<(u8, u64), EventDecodeError> {
        let initial = *self
            .bytes
            .get(self.offset)
            .ok_or(EventDecodeError::Truncated)?;
        self.offset += 1;
        let major = initial >> 5;
        let additional = initial & 0x1f;
        if additional < 24 {
            return Ok((major, u64::from(additional)));
        }
        let width = match additional {
            24 => 1,
            25 => 2,
            26 => 4,
            27 => 8,
            _ => return Err(EventDecodeError::NonCanonical),
        };
        let end = self
            .offset
            .checked_add(width)
            .ok_or(EventDecodeError::Truncated)?;
        let source = self
            .bytes
            .get(self.offset..end)
            .ok_or(EventDecodeError::Truncated)?;
        self.offset = end;
        let mut value = 0_u64;
        for byte in source {
            value = (value << 8) | u64::from(*byte);
        }
        let minimum = match width {
            1 => 24,
            2 => 256,
            4 => 65_536,
            _ => 4_294_967_296,
        };
        if value < minimum {
            return Err(EventDecodeError::NonCanonical);
        }
        Ok((major, value))
    }

    fn unsigned(&mut self) -> Result<u64, EventDecodeError> {
        let (major, value) = self.head()?;
        if major != 0 {
            return Err(EventDecodeError::WrongType);
        }
        Ok(value)
    }

    fn text(&mut self) -> Result<&'a str, EventDecodeError> {
        let (major, length) = self.head()?;
        if major != 3 {
            return Err(EventDecodeError::WrongType);
        }
        let length =
            usize::try_from(length).map_err(|_| EventDecodeError::Truncated)?;
        let end = self
            .offset
            .checked_add(length)
            .ok_or(EventDecodeError::Truncated)?;
        let source = self
            .bytes
            .get(self.offset..end)
            .ok_or(EventDecodeError::Truncated)?;
        self.offset = end;
        core::str::from_utf8(source).map_err(|_| EventDecodeError::InvalidUtf8)
    }
}

/// Decode the canonical host-owned semantic event envelope copied into guest
/// memory for `handle_event`.
pub fn decode_ui_event(bytes: &[u8]) -> Result<UiEvent<'_>, EventDecodeError> {
    let mut reader = CborReader { bytes, offset: 0 };
    let (major, fields) = reader.head()?;
    if major != 5 || fields != 7 {
        return Err(EventDecodeError::WrongShape);
    }
    for expected in 0_u64..=6 {
        if reader.unsigned()? != expected {
            return Err(EventDecodeError::NonCanonical);
        }
        match expected {
            0 => {
                if reader.unsigned()? != 1 {
                    return Err(EventDecodeError::UnsupportedVersion);
                }
            }
            1..=4 => {}
            5..=6 => {}
            _ => unreachable!(),
        }
        if expected == 0 {
            continue;
        }
        if expected <= 4 {
            // Text values are decoded below in a second, shape-preserving
            // pass so the returned slices retain the input lifetime.
            let _ = reader.text()?;
        } else {
            let _ = reader.unsigned()?;
        }
    }
    if reader.offset != bytes.len() {
        return Err(EventDecodeError::TrailingData);
    }

    let mut reader = CborReader { bytes, offset: 0 };
    let _ = reader.head()?;
    let _ = reader.unsigned()?;
    let _ = reader.unsigned()?;
    let _ = reader.unsigned()?;
    let app_id = reader.text()?;
    let _ = reader.unsigned()?;
    let screen_id = reader.text()?;
    let _ = reader.unsigned()?;
    let node_id = reader.text()?;
    let _ = reader.unsigned()?;
    let action_id = reader.text()?;
    let _ = reader.unsigned()?;
    let kind = match reader.unsigned()? {
        0 => EventKind::Tap,
        1 => EventKind::LongPress,
        2 => EventKind::Repeat,
        3 => EventKind::ValueChanging,
        4 => EventKind::ValueCommitted,
        5 => EventKind::CheckedChanged,
        6 => EventKind::PageChanged,
        7 => EventKind::Dismissed,
        8 => EventKind::Submit,
        9 => EventKind::Retry,
        10 => EventKind::Cancel,
        _ => return Err(EventDecodeError::UnsupportedEvent),
    };
    let _ = reader.unsigned()?;
    let timestamp_monotonic_ms = reader.unsigned()?;
    Ok(UiEvent {
        app_id,
        screen_id,
        node_id,
        action_id,
        kind,
        timestamp_monotonic_ms,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const TAP_EVENT: &[u8] = &[
        0xa7, 0x00, 0x01, 0x01, 0x65, b'h', b'e', b'l', b'l', b'o',
        0x02, 0x6c, b'h', b'e', b'l', b'l', b'o', b'.', b's', b'c',
        b'r', b'e', b'e', b'n', 0x03, 0x6c, b'h', b'e', b'l', b'l',
        b'o', b'.', b'a', b'c', b't', b'i', b'o', b'n', 0x04, 0x69,
        b's', b'a', b'y', b'_', b'h', b'e', b'l', b'l', b'o', 0x05,
        0x00, 0x06, 0x19, 0x03, 0xe8,
    ];

    #[test]
    fn decodes_canonical_semantic_event() {
        let event = decode_ui_event(TAP_EVENT).unwrap();
        assert_eq!(event.app_id, "hello");
        assert_eq!(event.screen_id, "hello.screen");
        assert_eq!(event.node_id, "hello.action");
        assert_eq!(event.action_id, "say_hello");
        assert_eq!(event.kind, EventKind::Tap);
        assert_eq!(event.timestamp_monotonic_ms, 1000);
    }

    #[test]
    fn rejects_trailing_and_noncanonical_data() {
        let mut trailing = [0_u8; TAP_EVENT.len() + 1];
        trailing[..TAP_EVENT.len()].copy_from_slice(TAP_EVENT);
        assert_eq!(
            decode_ui_event(&trailing),
            Err(EventDecodeError::TrailingData)
        );
        let mut unordered = [0_u8; TAP_EVENT.len()];
        unordered.copy_from_slice(TAP_EVENT);
        unordered[1] = 1;
        assert_eq!(
            decode_ui_event(&unordered),
            Err(EventDecodeError::NonCanonical)
        );
    }
}

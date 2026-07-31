#![no_std]

/// Host ABI version implemented by this SDK.
pub const HOST_ABI_VERSION: u32 = 1;

#[link(wasm_import_module = "doodad")]
unsafe extern "C" {
    #[link_name = "ui_mount"]
    fn host_ui_mount(pointer: *const u8, length: u32) -> i32;
    #[link_name = "timer_schedule_after"]
    fn host_timer_schedule_after(
        pointer: *const u8,
        length: u32,
        duration_ms: u32,
    ) -> u64;
    #[link_name = "timer_cancel"]
    fn host_timer_cancel(pointer: *const u8, length: u32) -> i32;
    #[link_name = "timer_acknowledge"]
    fn host_timer_acknowledge(pointer: *const u8, length: u32) -> i32;
    #[link_name = "provider_request"]
    fn host_provider_request(
        provider_pointer: *const u8,
        provider_length: u32,
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "calendar_request"]
    fn host_calendar_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "audio_request"]
    fn host_audio_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "medication_request"]
    fn host_medication_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "sensor_request"]
    fn host_sensor_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "sleep_request"]
    fn host_sleep_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "media_request"]
    fn host_media_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "navigation_request"]
    fn host_navigation_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "transit_request"]
    fn host_transit_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "home_request"]
    fn host_home_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "sports_request"]
    fn host_sports_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "wallet_request"]
    fn host_wallet_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "remote_request"]
    fn host_remote_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "workout_request"]
    fn host_workout_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
    #[link_name = "game_request"]
    fn host_game_request(
        operation_pointer: *const u8,
        operation_length: u32,
        payload_pointer: *const u8,
        payload_length: u32,
    ) -> u64;
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
pub enum SchedulerError {
    InvalidId,
    InvalidDuration,
    Rejected,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProviderRequestError {
    InvalidIdentifier,
    PayloadTooLarge,
    Rejected,
}

pub fn request_provider(
    provider_id: &str,
    operation_id: &str,
    payload: &[u8],
) -> Result<u64, ProviderRequestError> {
    if !valid_service_id(provider_id)
        || !valid_service_id(operation_id)
    {
        return Err(ProviderRequestError::InvalidIdentifier);
    }
    if payload.len() > 512 {
        return Err(ProviderRequestError::PayloadTooLarge);
    }
    let payload_pointer = if payload.is_empty() {
        core::ptr::null()
    } else {
        payload.as_ptr()
    };
    let request_id = unsafe {
        host_provider_request(
            provider_id.as_ptr(),
            provider_id.len() as u32,
            operation_id.as_ptr(),
            operation_id.len() as u32,
            payload_pointer,
            payload.len() as u32,
        )
    };
    if request_id == 0 {
        Err(ProviderRequestError::Rejected)
    } else {
        Ok(request_id)
    }
}

type BoundProviderRequest = unsafe extern "C" fn(
    *const u8,
    u32,
    *const u8,
    u32,
) -> u64;

fn request_bound_provider(
    operation_id: &str,
    payload: &[u8],
    request: BoundProviderRequest,
) -> Result<u64, ProviderRequestError> {
    if !valid_service_id(operation_id) {
        return Err(ProviderRequestError::InvalidIdentifier);
    }
    if payload.len() > 512 {
        return Err(ProviderRequestError::PayloadTooLarge);
    }
    let payload_pointer = if payload.is_empty() {
        core::ptr::null()
    } else {
        payload.as_ptr()
    };
    let request_id = unsafe {
        request(
            operation_id.as_ptr(),
            operation_id.len() as u32,
            payload_pointer,
            payload.len() as u32,
        )
    };
    if request_id == 0 {
        Err(ProviderRequestError::Rejected)
    } else {
        Ok(request_id)
    }
}

macro_rules! bound_provider {
    ($name:ident, $host:ident) => {
        pub fn $name(
            operation_id: &str,
            payload: &[u8],
        ) -> Result<u64, ProviderRequestError> {
            request_bound_provider(operation_id, payload, $host)
        }
    };
}

bound_provider!(request_calendar, host_calendar_request);
bound_provider!(request_audio, host_audio_request);
bound_provider!(request_medication, host_medication_request);
bound_provider!(request_sensor, host_sensor_request);
bound_provider!(request_sleep, host_sleep_request);
bound_provider!(request_media, host_media_request);
bound_provider!(request_navigation, host_navigation_request);
bound_provider!(request_transit, host_transit_request);
bound_provider!(request_home, host_home_request);
bound_provider!(request_sports, host_sports_request);
bound_provider!(request_wallet, host_wallet_request);
bound_provider!(request_remote, host_remote_request);
bound_provider!(request_workout, host_workout_request);
bound_provider!(request_game, host_game_request);

fn valid_service_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 48
        && value.as_bytes().iter().all(|byte| {
            byte.is_ascii_lowercase()
                || byte.is_ascii_digit()
                || matches!(*byte, b'.' | b'-' | b'_')
        })
}

pub fn schedule_timer_after(
    id: &str,
    duration_ms: u32,
) -> Result<u64, SchedulerError> {
    if !valid_service_id(id) {
        return Err(SchedulerError::InvalidId);
    }
    if duration_ms == 0 {
        return Err(SchedulerError::InvalidDuration);
    }
    let deadline = unsafe {
        host_timer_schedule_after(
            id.as_ptr(),
            id.len() as u32,
            duration_ms,
        )
    };
    if deadline == 0 {
        Err(SchedulerError::Rejected)
    } else {
        Ok(deadline)
    }
}

pub fn cancel_timer(id: &str) -> Result<(), SchedulerError> {
    if !valid_service_id(id) {
        return Err(SchedulerError::InvalidId);
    }
    let accepted =
        unsafe { host_timer_cancel(id.as_ptr(), id.len() as u32) };
    if accepted == 1 {
        Ok(())
    } else {
        Err(SchedulerError::Rejected)
    }
}

pub fn acknowledge_timer(id: &str) -> Result<(), SchedulerError> {
    if !valid_service_id(id) {
        return Err(SchedulerError::InvalidId);
    }
    let accepted = unsafe {
        host_timer_acknowledge(id.as_ptr(), id.len() as u32)
    };
    if accepted == 1 {
        Ok(())
    } else {
        Err(SchedulerError::Rejected)
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
    pub value: EventValue<'a>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EventValue<'a> {
    None,
    Integer(i32),
    Boolean(bool),
    Text(&'a str),
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

    fn signed(&mut self) -> Result<i64, EventDecodeError> {
        let (major, argument) = self.head()?;
        match major {
            0 => i64::try_from(argument)
                .map_err(|_| EventDecodeError::WrongType),
            1 => {
                let value = i64::try_from(argument)
                    .map_err(|_| EventDecodeError::WrongType)?;
                Ok(-1 - value)
            }
            _ => Err(EventDecodeError::WrongType),
        }
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

    fn bytes(&mut self) -> Result<&'a [u8], EventDecodeError> {
        let (major, length) = self.head()?;
        if major != 2 {
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
        Ok(source)
    }

    fn event_value(&mut self) -> Result<EventValue<'a>, EventDecodeError> {
        let (major, argument) = self.head()?;
        match major {
            0 => {
                let value =
                    i32::try_from(argument).map_err(|_| EventDecodeError::WrongType)?;
                Ok(EventValue::Integer(value))
            }
            1 => {
                let magnitude =
                    i32::try_from(argument).map_err(|_| EventDecodeError::WrongType)?;
                Ok(EventValue::Integer(-1 - magnitude))
            }
            3 => {
                let length = usize::try_from(argument)
                    .map_err(|_| EventDecodeError::Truncated)?;
                let end = self
                    .offset
                    .checked_add(length)
                    .ok_or(EventDecodeError::Truncated)?;
                let source = self
                    .bytes
                    .get(self.offset..end)
                    .ok_or(EventDecodeError::Truncated)?;
                self.offset = end;
                let text = core::str::from_utf8(source)
                    .map_err(|_| EventDecodeError::InvalidUtf8)?;
                Ok(EventValue::Text(text))
            }
            7 if argument == 20 => Ok(EventValue::Boolean(false)),
            7 if argument == 21 => Ok(EventValue::Boolean(true)),
            _ => Err(EventDecodeError::WrongType),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Freshness {
    Current = 0,
    Stale = 1,
    Offline = 2,
    Error = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ProviderEvent<'a> {
    pub provider_id: &'a str,
    pub event_id: &'a str,
    pub revision: u64,
    pub freshness: Freshness,
    pub observed_at_ms: u64,
    pub payload: &'a [u8],
}

pub fn decode_provider_event(
    bytes: &[u8],
) -> Result<ProviderEvent<'_>, EventDecodeError> {
    let mut reader = CborReader { bytes, offset: 0 };
    let (major, fields) = reader.head()?;
    if major != 5 || fields != 7 {
        return Err(EventDecodeError::WrongShape);
    }
    if reader.unsigned()? != 0 || reader.unsigned()? != 1 {
        return Err(EventDecodeError::UnsupportedVersion);
    }
    if reader.unsigned()? != 1 {
        return Err(EventDecodeError::NonCanonical);
    }
    let provider_id = reader.text()?;
    if reader.unsigned()? != 2 {
        return Err(EventDecodeError::NonCanonical);
    }
    let event_id = reader.text()?;
    if reader.unsigned()? != 3 {
        return Err(EventDecodeError::NonCanonical);
    }
    let revision = reader.unsigned()?;
    if reader.unsigned()? != 4 {
        return Err(EventDecodeError::NonCanonical);
    }
    let freshness = match reader.unsigned()? {
        0 => Freshness::Current,
        1 => Freshness::Stale,
        2 => Freshness::Offline,
        3 => Freshness::Error,
        _ => return Err(EventDecodeError::WrongShape),
    };
    if reader.unsigned()? != 5 {
        return Err(EventDecodeError::NonCanonical);
    }
    let observed_at_ms = reader.unsigned()?;
    if reader.unsigned()? != 6 {
        return Err(EventDecodeError::NonCanonical);
    }
    let payload = reader.bytes()?;
    if reader.offset != bytes.len() {
        return Err(EventDecodeError::TrailingData);
    }
    Ok(ProviderEvent {
        provider_id,
        event_id,
        revision,
        freshness,
        observed_at_ms,
        payload,
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum TimerProviderState {
    Scheduled = 0,
    Firing = 1,
    Acknowledged = 2,
    Cancelled = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct TimerProviderPayload<'a> {
    pub id: &'a str,
    pub state: TimerProviderState,
    pub remaining_ms: u64,
    pub deadline_scenario_ms: u64,
    pub firing_ordinal: u64,
}

pub fn decode_timer_provider_payload(
    bytes: &[u8],
) -> Result<TimerProviderPayload<'_>, EventDecodeError> {
    let mut reader = CborReader { bytes, offset: 0 };
    let (major, fields) = reader.head()?;
    if major != 5 || fields != 5 {
        return Err(EventDecodeError::WrongShape);
    }
    if reader.unsigned()? != 0 {
        return Err(EventDecodeError::NonCanonical);
    }
    let id = reader.text()?;
    if reader.unsigned()? != 1 {
        return Err(EventDecodeError::NonCanonical);
    }
    let state = match reader.unsigned()? {
        0 => TimerProviderState::Scheduled,
        1 => TimerProviderState::Firing,
        2 => TimerProviderState::Acknowledged,
        3 => TimerProviderState::Cancelled,
        _ => return Err(EventDecodeError::WrongShape),
    };
    if reader.unsigned()? != 2 {
        return Err(EventDecodeError::NonCanonical);
    }
    let remaining_ms = reader.unsigned()?;
    if reader.unsigned()? != 3 {
        return Err(EventDecodeError::NonCanonical);
    }
    let deadline_scenario_ms = reader.unsigned()?;
    if reader.unsigned()? != 4 {
        return Err(EventDecodeError::NonCanonical);
    }
    let firing_ordinal = reader.unsigned()?;
    if reader.offset != bytes.len() {
        return Err(EventDecodeError::TrailingData);
    }
    Ok(TimerProviderPayload {
        id,
        state,
        remaining_ms,
        deadline_scenario_ms,
        firing_ordinal,
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WeatherProviderPayload<'a> {
    pub temperature_tenths_f: i32,
    pub condition: &'a str,
    pub detail: &'a str,
    pub location: &'a str,
    pub data_revision: u64,
    pub cache_age_minutes: u64,
}

pub fn decode_weather_provider_payload(
    bytes: &[u8],
) -> Result<WeatherProviderPayload<'_>, EventDecodeError> {
    let mut reader = CborReader { bytes, offset: 0 };
    let (major, fields) = reader.head()?;
    if major != 5 || fields != 6 {
        return Err(EventDecodeError::WrongShape);
    }
    if reader.unsigned()? != 0 {
        return Err(EventDecodeError::NonCanonical);
    }
    let temperature_tenths_f = i32::try_from(reader.signed()?)
        .map_err(|_| EventDecodeError::WrongType)?;
    if reader.unsigned()? != 1 {
        return Err(EventDecodeError::NonCanonical);
    }
    let condition = reader.text()?;
    if reader.unsigned()? != 2 {
        return Err(EventDecodeError::NonCanonical);
    }
    let detail = reader.text()?;
    if reader.unsigned()? != 3 {
        return Err(EventDecodeError::NonCanonical);
    }
    let location = reader.text()?;
    if reader.unsigned()? != 4 {
        return Err(EventDecodeError::NonCanonical);
    }
    let data_revision = reader.unsigned()?;
    if reader.unsigned()? != 5 {
        return Err(EventDecodeError::NonCanonical);
    }
    let cache_age_minutes = reader.unsigned()?;
    if reader.offset != bytes.len() {
        return Err(EventDecodeError::TrailingData);
    }
    Ok(WeatherProviderPayload {
        temperature_tenths_f,
        condition,
        detail,
        location,
        data_revision,
        cache_age_minutes,
    })
}

/// Decode the canonical host-owned semantic event envelope copied into guest
/// memory for `handle_event`.
pub fn decode_ui_event(bytes: &[u8]) -> Result<UiEvent<'_>, EventDecodeError> {
    let mut reader = CborReader { bytes, offset: 0 };
    let (major, fields) = reader.head()?;
    if major != 5 || (fields != 7 && fields != 8) {
        return Err(EventDecodeError::WrongShape);
    }
    if reader.unsigned()? != 0 {
        return Err(EventDecodeError::NonCanonical);
    }
    if reader.unsigned()? != 1 {
        return Err(EventDecodeError::UnsupportedVersion);
    }
    if reader.unsigned()? != 1 {
        return Err(EventDecodeError::NonCanonical);
    }
    let app_id = reader.text()?;
    if reader.unsigned()? != 2 {
        return Err(EventDecodeError::NonCanonical);
    }
    let screen_id = reader.text()?;
    if reader.unsigned()? != 3 {
        return Err(EventDecodeError::NonCanonical);
    }
    let node_id = reader.text()?;
    if reader.unsigned()? != 4 {
        return Err(EventDecodeError::NonCanonical);
    }
    let action_id = reader.text()?;
    if reader.unsigned()? != 5 {
        return Err(EventDecodeError::NonCanonical);
    }
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
    if reader.unsigned()? != 6 {
        return Err(EventDecodeError::NonCanonical);
    }
    let timestamp_monotonic_ms = reader.unsigned()?;
    let value = if fields == 8 {
        if reader.unsigned()? != 7 {
            return Err(EventDecodeError::NonCanonical);
        }
        reader.event_value()?
    } else {
        EventValue::None
    };
    if reader.offset != bytes.len() {
        return Err(EventDecodeError::TrailingData);
    }
    Ok(UiEvent {
        app_id,
        screen_id,
        node_id,
        action_id,
        kind,
        timestamp_monotonic_ms,
        value,
    })
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CommandEncodeError {
    Capacity,
    InvalidCount,
    InvalidText,
    WrongCommandCount,
}

/// Bounded canonical CommandBatch encoder for guest-owned in-place UI updates.
///
/// Call `begin`, append exactly the declared number of commands, then `finish`.
/// The returned slice is guest-owned and may be packed with `pack_result`.
pub struct UiCommandBuffer<const N: usize> {
    bytes: [u8; N],
    length: usize,
    expected: u8,
    written: u8,
}

impl<const N: usize> UiCommandBuffer<N> {
    pub const fn new() -> Self {
        Self {
            bytes: [0; N],
            length: 0,
            expected: 0,
            written: 0,
        }
    }

    pub fn begin(&mut self, command_count: u8) -> Result<(), CommandEncodeError> {
        if command_count == 0 || command_count > 23 {
            return Err(CommandEncodeError::InvalidCount);
        }
        self.length = 0;
        self.expected = command_count;
        self.written = 0;
        self.byte(0xa2)?;
        self.byte(0x00)?;
        self.byte(0x01)?;
        self.byte(0x01)?;
        self.byte(0x80 | command_count)?;
        Ok(())
    }

    pub fn set_primary_text(
        &mut self,
        target: &str,
        value: &str,
    ) -> Result<(), CommandEncodeError> {
        self.set_text_property(target, 0, value)
    }

    pub fn set_secondary_text(
        &mut self,
        target: &str,
        value: &str,
    ) -> Result<(), CommandEncodeError> {
        self.set_text_property(target, 1, value)
    }

    pub fn set_value(
        &mut self,
        target: &str,
        value: i64,
    ) -> Result<(), CommandEncodeError> {
        self.set_integer_property(target, 2, value)
    }

    pub fn set_maximum(
        &mut self,
        target: &str,
        value: i64,
    ) -> Result<(), CommandEncodeError> {
        self.set_integer_property(target, 3, value)
    }

    pub fn set_visible(
        &mut self,
        target: &str,
        visible: bool,
    ) -> Result<(), CommandEncodeError> {
        self.command_slot()?;
        self.byte(0xa3)?;
        self.byte(0x00)?;
        self.byte(0x01)?;
        self.byte(0x01)?;
        self.text(target)?;
        self.byte(0x03)?;
        self.byte(if visible { 0xf5 } else { 0xf4 })?;
        self.written += 1;
        Ok(())
    }

    pub fn set_enabled(
        &mut self,
        target: &str,
        enabled: bool,
    ) -> Result<(), CommandEncodeError> {
        self.command_slot()?;
        self.byte(0xa3)?;
        self.byte(0x00)?;
        self.byte(0x02)?;
        self.byte(0x01)?;
        self.text(target)?;
        self.byte(0x03)?;
        self.byte(if enabled { 0xf5 } else { 0xf4 })?;
        self.written += 1;
        Ok(())
    }

    pub fn finish(&self) -> Result<&[u8], CommandEncodeError> {
        if self.written != self.expected {
            return Err(CommandEncodeError::WrongCommandCount);
        }
        Ok(&self.bytes[..self.length])
    }

    fn set_text_property(
        &mut self,
        target: &str,
        property: u8,
        value: &str,
    ) -> Result<(), CommandEncodeError> {
        self.command_slot()?;
        self.byte(0xa4)?;
        self.byte(0x00)?;
        self.byte(0x00)?;
        self.byte(0x01)?;
        self.text(target)?;
        self.byte(0x02)?;
        self.byte(property)?;
        self.byte(0x03)?;
        self.text(value)?;
        self.written += 1;
        Ok(())
    }

    fn set_integer_property(
        &mut self,
        target: &str,
        property: u8,
        value: i64,
    ) -> Result<(), CommandEncodeError> {
        self.command_slot()?;
        self.byte(0xa4)?;
        self.byte(0x00)?;
        self.byte(0x00)?;
        self.byte(0x01)?;
        self.text(target)?;
        self.byte(0x02)?;
        self.byte(property)?;
        self.byte(0x03)?;
        self.signed_integer(value)?;
        self.written += 1;
        Ok(())
    }

    fn signed_integer(&mut self, value: i64) -> Result<(), CommandEncodeError> {
        if value >= 0 {
            self.unsigned_integer(0, value as u64)
        } else {
            self.unsigned_integer(1, (-1 - value) as u64)
        }
    }

    fn unsigned_integer(
        &mut self,
        major: u8,
        value: u64,
    ) -> Result<(), CommandEncodeError> {
        if value < 24 {
            return self.byte((major << 5) | value as u8);
        }
        if value <= u8::MAX as u64 {
            self.byte((major << 5) | 24)?;
            return self.byte(value as u8);
        }
        if value <= u16::MAX as u64 {
            self.byte((major << 5) | 25)?;
            self.byte((value >> 8) as u8)?;
            return self.byte(value as u8);
        }
        if value <= u32::MAX as u64 {
            self.byte((major << 5) | 26)?;
            for shift in (0..=24).rev().step_by(8) {
                self.byte((value >> shift) as u8)?;
            }
            return Ok(());
        }
        self.byte((major << 5) | 27)?;
        for shift in (0..=56).rev().step_by(8) {
            self.byte((value >> shift) as u8)?;
        }
        Ok(())
    }

    fn command_slot(&self) -> Result<(), CommandEncodeError> {
        if self.expected == 0 || self.written >= self.expected {
            Err(CommandEncodeError::WrongCommandCount)
        } else {
            Ok(())
        }
    }

    fn text(&mut self, value: &str) -> Result<(), CommandEncodeError> {
        let length = value.as_bytes().len();
        if length == 0 || length > 255 {
            return Err(CommandEncodeError::InvalidText);
        }
        if length < 24 {
            self.byte(0x60 | length as u8)?;
        } else {
            self.byte(0x78)?;
            self.byte(length as u8)?;
        }
        self.copy(value.as_bytes())
    }

    fn byte(&mut self, value: u8) -> Result<(), CommandEncodeError> {
        if self.length >= N {
            return Err(CommandEncodeError::Capacity);
        }
        self.bytes[self.length] = value;
        self.length += 1;
        Ok(())
    }

    fn copy(&mut self, value: &[u8]) -> Result<(), CommandEncodeError> {
        let end = self
            .length
            .checked_add(value.len())
            .ok_or(CommandEncodeError::Capacity)?;
        if end > N {
            return Err(CommandEncodeError::Capacity);
        }
        self.bytes[self.length..end].copy_from_slice(value);
        self.length = end;
        Ok(())
    }
}

impl<const N: usize> Default for UiCommandBuffer<N> {
    fn default() -> Self {
        Self::new()
    }
}

pub fn pack_result(bytes: &[u8]) -> u64 {
    if bytes.is_empty() || bytes.len() > u32::MAX as usize {
        return 0;
    }
    ((bytes.as_ptr() as u64) << 32) | bytes.len() as u64
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
        assert_eq!(event.value, EventValue::None);
    }

    #[test]
    fn decodes_typed_event_values() {
        let mut text_event = TAP_EVENT.to_vec();
        text_event[0] = 0xa8;
        text_event.extend_from_slice(&[0x07, 0x61, b'7']);
        assert_eq!(
            decode_ui_event(&text_event).unwrap().value,
            EventValue::Text("7")
        );

        let mut negative_event = TAP_EVENT.to_vec();
        negative_event[0] = 0xa8;
        negative_event.extend_from_slice(&[0x07, 0x24]);
        assert_eq!(
            decode_ui_event(&negative_event).unwrap().value,
            EventValue::Integer(-5)
        );

        let mut boolean_event = TAP_EVENT.to_vec();
        boolean_event[0] = 0xa8;
        boolean_event.extend_from_slice(&[0x07, 0xf5]);
        assert_eq!(
            decode_ui_event(&boolean_event).unwrap().value,
            EventValue::Boolean(true)
        );
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

    #[test]
    fn encodes_bounded_canonical_ui_command_batches() {
        let mut commands = UiCommandBuffer::<128>::new();
        commands.begin(2).unwrap();
        commands
            .set_primary_text("calculator.result", "12.5")
            .unwrap();
        commands
            .set_visible("calculator.error", false)
            .unwrap();
        let bytes = commands.finish().unwrap();
        assert_eq!(bytes[0], 0xa2);
        assert_eq!(bytes[4], 0x82);
        assert_ne!(pack_result(bytes), 0);

        let mut numeric = UiCommandBuffer::<128>::new();
        numeric.begin(2).unwrap();
        numeric.set_value("timer.duration", 2).unwrap();
        numeric.set_maximum("timer.duration", 60).unwrap();
        let bytes = numeric.finish().unwrap();
        assert!(bytes.windows(4).any(|part| part == [0x02, 0x02, 0x03, 0x02]));
        assert!(bytes.windows(5).any(
            |part| part == [0x02, 0x03, 0x03, 0x18, 0x3c],
        ));

        let mut too_small = UiCommandBuffer::<8>::new();
        too_small.begin(1).unwrap();
        assert_eq!(
            too_small.set_primary_text("long.target", "value"),
            Err(CommandEncodeError::Capacity)
        );
    }

    #[test]
    fn decodes_versioned_timer_provider_events() {
        let payload: &[u8] = &[
            0xa5,
            0x00,
            0x6d,
            b't', b'i', b'm', b'e', b'r', b'.', b'p', b'r', b'i', b'm',
            b'a', b'r', b'y',
            0x01, 0x00,
            0x02, 0x19, 0x0b, 0xb8,
            0x03, 0x19, 0x0f, 0xa0,
            0x04, 0x00,
        ];
        let mut envelope = [
            0xa7,
            0x00, 0x01,
            0x01, 0x6f,
            b'e', b'x', b'a', b'c', b't', b'_', b's', b'c', b'h', b'e',
            b'd', b'u', b'l', b'e', b'r',
            0x02, 0x6d,
            b't', b'i', b'm', b'e', b'r', b'.', b'c', b'h', b'a', b'n',
            b'g', b'e', b'd',
            0x03, 0x01,
            0x04, 0x00,
            0x05, 0x18, 0x64,
            0x06, 0x58, payload.len() as u8,
        ]
        .to_vec();
        envelope.extend_from_slice(payload);
        let event = decode_provider_event(&envelope).unwrap();
        assert_eq!(event.provider_id, "exact_scheduler");
        assert_eq!(event.event_id, "timer.changed");
        assert_eq!(event.revision, 1);
        assert_eq!(event.freshness, Freshness::Current);
        assert_eq!(event.observed_at_ms, 100);
        let timer = decode_timer_provider_payload(event.payload).unwrap();
        assert_eq!(timer.id, "timer.primary");
        assert_eq!(timer.state, TimerProviderState::Scheduled);
        assert_eq!(timer.remaining_ms, 3_000);
        assert_eq!(timer.deadline_scenario_ms, 4_000);
        assert_eq!(timer.firing_ordinal, 0);
    }
}

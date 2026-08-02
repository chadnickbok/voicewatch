# AppSpec v1

AppSpec is the narrow generated-app language above the native Material
framework. It describes intent and semantics; it is not a serialized LVGL
tree.

## Contract

Every document contains:

```json
{
  "schema_version": 1,
  "app_id": "calories",
  "screen": {
    "id": "today",
    "type": "screen",
    "props": {"gap": "sm", "align": "stretch", "children": []}
  }
}
```

The initial vocabulary is:

- layout: `screen`, `column`, `row`, `scroll`;
- content: `text`, `card`, `live_card`;
- action: `button`, `stepper`, `toggle`, `keypad`, `voice_orb`;
- status: `progress`.

Public choices are bounded semantic values such as `primary`, `secondary`,
`neutral`, `compact`, `default`, `large`, `sm`, and `md`. There is no raw color,
font, radius, coordinate, transform, z-index, image URL, haptic waveform,
animation curve, LVGL widget, or style dictionary.

Interactive nodes require both a useful semantic label and a semantic event
mapped to an `ActionId`. IDs are unique, stable lowercase identifiers.
The native renderer translates LVGL input to that semantic event, posts it to
the serialized app actor, and delivers a canonical CBOR envelope to the
guest's `handle_event` export. The Hello fixture exercises the complete
button → LVGL event → actor queue → WAMR export → canonical CommandBatch →
in-place native patch path.

Control events add one optional typed value without changing ordinary button
events: keypad events carry the exact key string, steppers carry the new
bounded integer value, and toggles carry their checked boolean state. Events
without a value retain the original seven-field canonical envelope. This
keeps existing guests compatible while allowing generated apps to respond to
real input rather than guessing from a node ID.

## Launch-surface policy

A launched app owns the complete 240×240 app surface. Its initial AppSpec must
not spend a row repeating the manifest name, app name, or current launcher
label. The launcher already established app identity before navigation.

The first visible node should therefore be useful app content: a hero value,
current state, actionable control, or a genuinely contextual label. Context
such as a city name or the current exercise/set may remain when removing it
would make the screen ambiguous. A decorative `Calculator`, `Tasks`, or
`Media Remote` heading may not.

The trusted shell may show status chrome while loading, recovering, or
reporting a host error. A successful AppSpec mount destroys that transient
chrome and renders full-screen. Renderers must not synthesize an app title bar
around the mounted document.

## Resource and safety limits

- 250 nodes hard maximum;
- depth 12 hard maximum;
- 32 children per container;
- one primary scroll axis;
- at most 64 patch operations per native transaction;
- at most 4096 canonical-CBOR bytes per returned command batch;
- a command batch is exclusively UI or state domain, never mixed;
- duplicate IDs and invalid parents fail before mount;
- every patch transaction is staged and fully validated before commit;
- a failed transaction leaves generation and mounted state unchanged.

The checked-in JSON Schema provides structural tooling. The Python validator
adds cross-node and semantic rules. The native fixed-capacity validators and
reconciler enforce the device boundary without exceptions or RTTI. AppSpec
documents and returned command batches are copied into host-owned bounded
storage before use.

State follows the same fail-before-commit model. The native store supports
typed boolean, integer, Q16.16 fixed-point, and bounded string values beneath
`screen.*`, `app.*`, `shared.*`, `system.*`, and `session.*`. `system.*` is
read-only; a `shared.*` write requires a matching manifest permission. A failed
transaction cannot advance the revision or partially change state.

Properties can be literals or typed bindings. Bindings name a state path and
may add one bounded host-safe format or predicate:

```json
{
  "text": {
    "bind": "shared.nutrition.total",
    "format": {"kind": "number", "unit": "kcal"}
  }
}
```

```json
{
  "visible": {
    "bind": "shared.nutrition.total",
    "predicate": {"op": "greater_than", "value": 0}
  }
}
```

The initial formats are `raw`, `number`, and `duration`; the initial
predicates are `exists`, `equals`, `not_equals`, `less_than`, and
`greater_than`. There is deliberately no expression string or embedded
program. Derived business values remain guest logic. Native `BindingHub`
owns at most 64 bindings per mounted screen, observes store revisions, stages
every affected property, and commits one reconciler transaction. A missing
state value, type mismatch, or stale node fails without a partial UI update.

## Preview

```bash
./doodad appspec apps/calories/appspec.json --validate-only
./doodad appspec apps/calories/appspec.json \
  --output target/appspec/calories.bmp
```

Preview compiles JSON to the canonical CBOR device representation, passes it
through the same fixed-capacity C++ decoder used by firmware, and mounts the
same native Material components through `m3e_lvgl`. The current hard wire limit
is 4096 bytes. Indefinite-length CBOR, non-minimal integers, unordered or
duplicate numeric keys, invalid UTF-8, unknown fields, and quota violations
fail before rendering.

## Package navigation

A guest may navigate by synchronously mounting another bounded AppSpec from
its event handler and returning zero to indicate that no follow-up
CommandBatch is required. Nonzero results remain packed borrowed CommandBatch
slices. Screen changes remount; ordinary state changes patch the current
screen in place. Supported patches cover primary/secondary text, numeric
value/maximum, chart samples, icon identity, semantic label/value,
visibility, enabled state, and the boolean checked state of a toggle.

## Reference fixtures

- `apps/hello/appspec.json`
- `apps/calories/appspec.json`
- `apps/calculator/appspec.json`
- `apps/workout/appspec.json`
- `apps/voice/appspec.json`

Their corresponding native Material catalog stories render the real component
implementation used by firmware.

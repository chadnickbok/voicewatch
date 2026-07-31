# Material fidelity matrix

Fidelity labels describe the intended implementation and current evidence:

- **Exact:** verified metrics, states, tokens, motion, oracle images, and tests.
- **Equivalent:** same semantic role with an approved square/input adaptation.
- **Inspired:** product-native component built from Material tokens.
- **Deferred:** intentionally absent with a documented fallback.
- **Planned:** no fidelity claim has been earned yet.

The initial Android oracle and host-golden lane is checked in, but no component
yet qualifies as Exact. Exact still requires reviewed API 37 runtime evidence
and per-state Compose-to-LVGL comparison. “Prototype” below means production
LVGL source, native behavior tests, deterministic RGB565 catalog evidence and,
where applicable, an initial Compose story exist; cross-renderer fidelity has
not yet been earned.

| Area | Component or behavior | Target | Current evidence |
|---|---|---:|---|
| Foundation | MaterialTheme | Exact | Token-complete prototype; live swap/1,000-cycle test |
| Foundation | Text and semantic typography | Exact | Semantic-role prototype |
| Foundation | AnimatedText | Equivalent | Paired-label slide/crossfade prototype |
| Foundation | Icon | Exact | Bounded LVGL symbol subset prototype |
| Scaffold | AppScaffold | Equivalent | Square-layer prototype |
| Scaffold | ScreenScaffold | Equivalent | Square content/time prototype |
| Scaffold | TimeText | Equivalent | Clock/status prototype |
| Actions | Button family | Exact | Variant/size/state prototype |
| Actions | CompactButton | Exact | 40px visual prototype |
| Actions | Icon/Text toggle families | Exact | Checked/disabled prototypes |
| Actions | ButtonGroup | Exact | Retargetable press/release width-motion prototype |
| Content | Card family | Exact | Title/body/clickable prototype |
| Content | ListHeader/ListSubHeader | Exact | Native prototypes |
| Progress | circular/segmented/linear | Exact | Native bar/arc/multi-arc prototypes |
| Input | Slider and Stepper | Exact | Stepped/limit prototypes |
| Selection | checkbox/radio/switch rows | Exact | Whole-row prototypes |
| Dialog | AlertDialog | Equivalent | Modal prototype |
| Dialog | ConfirmationDialog | Exact | Status-modal prototype |
| Picker | Picker/PickerGroup/date/time | Equivalent | Bounded date and 12/24-hour compositions; locale oracle pending |
| Paging | HorizontalPager/scaffold/indicator | Equivalent | Tileview/indicator prototypes |
| Navigation | SwipeToDismissBox | Equivalent | Square threshold/cancel-event prototype |
| Scroll | ScrollIndicator | Equivalent | Linear square prototype |
| Lists | TransformingLazyColumn | Equivalent | Eight-object virtual window + fixed-point transforms |
| Lists | SurfaceTransformation/snapping | Equivalent | Native object transform; anchor/snap formulas |
| Motion | AnimatedPage/FadingExpandingLabel | Equivalent | Two-slot transition and bounded expansion prototypes |
| Motion | expressive springs and interruption | Equivalent | Fixed-point evaluator plus retargetable component animations |
| Interaction | SwipeToReveal/split selection | Equivalent | Semantic threshold and independent-affordance prototypes |
| Shape | compatible shape morph states | Equivalent | Bounded radius-morph prototype |
| Round-only | EdgeButton | Deferred | Full-width square bottom action |
| Round-only | curved text/arc rendering | Deferred | Expose role; no square renderer |
| Companion | OpenOnPhoneDialog | Deferred | ContinueElsewhereDialog if needed |
| System | VoiceOrb | Inspired | Native contract/catalog prototype |
| System | VoiceOverlay/Transcript | Inspired | Bounded composition prototypes |
| System | ClarificationChoiceGroup | Inspired | Three-choice-plus-cancel prototype |
| System | ChangeReview/BuildProgress | Inspired | Structured/staged prototypes |
| Home | LiveCard/Glance | Inspired | Native bounded contract/catalog prototype |

The machine-readable disposition and implementation tier live in
`component-matrix.yaml`.

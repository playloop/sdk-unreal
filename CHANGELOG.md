# Changelog

## 0.5.0 Git distribution

- Install from the public Git repository using the pinned v0.5.0 tag.
- Updated setup and release instructions for Git installation.


## [Unreleased]

### Added

- **Player Bug Reports.** `UPlayloopClient::SubmitBugReport(Title, Description,
  Severity, OnDone)` (plus `SubmitBugReportWithContext` for the optional player
  id / session / idempotency key / device context) posts a player-initiated bug
  report to Playloop. Free for every studio. The report is stored, shows in the
  studio dashboard, and routes to the studio's connected issue tracker
  automatically. Build version / environment / platform are auto-filled; a
  per-call idempotency key makes a transport retry land once; a disabled client
  (no API key) fails soft with `bOk = false` and no network. New Blueprint types
  in `PlayloopTypes.h`: `EPlayloopBugSeverity`, `FPlayloopBugReportContext`,
  `FPlayloopBugReportParams`, `FPlayloopBugReportResult`, and the
  `FOnPlayloopBugReportSubmitted` / `…BP` delegates.
- **`UPlayloopBugReportWidget`** (UMG, programmatic): a drop-in report form
  (Title + Description + a four-option Severity selector, default Medium) built
  in `RebuildWidget` with no `.uasset`. Three integration paths, mirroring the
  feedback widget: spawn the default via `CreateDefault(World, Client)`, subclass
  in UMG for designer styling, or bring your own widget and call
  `SubmitBugReport` directly. Fires the `OnBugReportResolved` multicast on
  submit / cancel / failure.
- `PlayloopBugReportSpec.cpp`: automation specs for the bug-report logic layer
  (body shape + auto context, idempotency-key generation, validation rejects,
  disabled soft-fail, response mapping incl. idempotent) via an injected
  synchronous sender.

### Fixed

- Offline sessions no longer grow memory without bound: the pending-event
  buffer is capped and drops the oldest events once full, mirroring the tail
  buffer.
- Failed telemetry requests now honor the exponential backoff between retries
  instead of retrying almost immediately.
- The "Powered by Playloop" mark on the default feedback form is warmed off
  the game thread, so the first time a player opens the form doesn't stutter.
- The crash trap is installed only once a client actually starts with crash
  handling enabled, so a project that links the plugin but never configures
  Playloop leaves no stray pending-crashes file behind (matches the documented
  "installs on Start()" behavior).

### Changed

- Setup is now just the ingest key. The `GameSlug` and `GameId` settings
  (and the matching `FPlayloopConfig` fields) are gone. The key alone
  identifies your game, so telemetry, A/B experiments, and Tester Keys all
  attribute to it automatically with nothing else to configure. The editor
  settings window drops the "Game slug" and "Game id" fields.

### Removed

- `FPlayloopConfig.GameSlug` / `FPlayloopConfig.GameId` and the
  `UPlayloopSettings` properties of the same name.

## [0.5.0] - 2026-05-23

Initial release of the Playloop Unreal SDK, joining Unity, Godot,
Python, and TypeScript at the shared `0.5.0` pre-launch version pin.
All five SDKs share the same wire format, retry policy, and event
contract so the dashboard's per-engine attribution stays consistent.

### Added

- `UPlayloopClient` UObject with the full parity surface: `Configure`,
  `Start`, `Stop`, `Flush`, `EndSession`, `Track`, `TrackJson`,
  `SetStateBool` / `Number` / `String`, `IncrementState`, `ClearState`,
  `LinkTester`, `GetCurrentTester`, `UnlinkTester`, `ReportCrash`,
  static `GetCurrent`.
- Every public method is `UFUNCTION(BlueprintCallable)` so the same
  surface is reachable from Blueprint graphs.
- `UPlayloopSettings : UDeveloperSettings`, settings live under
  Project Settings, Plugins, Playloop and persist to
  `Config/DefaultPlayloop.ini`.
- `UPlayloopSubsystem` (a `UGameInstanceSubsystem`) auto-starts the client on
  GameInstance init when `bAutoStart` is true and credentials are set.
- Auto-batch + heartbeat timers (configurable via `FlushIntervalMs` /
  `HeartbeatIntervalMs`, set to 0 to disable).
- Crash trap via `FCoreDelegates::OnHandleSystemError`, persists to
  `<ProjectSavedDir>/Playloop/pending-crashes.json` (64KB cap, oldest
  trimmed) and drains as `crash_reported` events on next session
  start. Gated by `bEnableCrashHandler`.
- HTTP client with exponential backoff retry (3 attempts default, 500ms
  base, 5s cap, +/-25% jitter). Matches Unity / Godot / Python /
  TypeScript SDK behavior exactly.
- Stable device id auto-resolves to a v4 GUID persisted at
  `<ProjectSavedDir>/Playloop/device.id`.
- Top-level **Playloop** menu in the editor menu bar via `UToolMenus`:
  Settings, Send Test Event, Set Environment submenu, Docs submenu
  (Open Docs, Open Roadmap), Open Dashboard for This Game, Open
  GitHub Repo.
- Automation Spec test suite covering event queue, JSON envelope shape,
  crash persist/drain round-trip, client state accumulator, device id
  resolution. Run via `Window, Developer Tools, Session Frontend,
  Automation` or headless `UnrealEditor-Cmd -ExecCmds="Automation
  RunTests Playloop"`.
- `X-Playloop-SDK: unreal` header on every request so the dashboard's
  `/integrations` index attributes the session as `unreal-telemetry`.
- `SETUP_UNREAL.md` agent-facing install guide.
- `examples/heatmap.md`, `examples/crash-handler.md`.
- `README.md` and MIT `LICENSE`.

### Engine support

- Unreal Engine 5.3+.
- Targets Win64, Mac, Linux, Android, iOS, consoles.

### Fixed

- Dropped the pinned `EngineVersion` field from `PlayloopSDK.uplugin`. As a
  from-source plugin it compiles against whatever engine opens it, so pinning a
  single version made the editor flag it "incompatible" on any engine other than
  5.3 (e.g. 5.4+). The 5.3 minimum is documented in the README; it no longer
  needs to be enforced by the descriptor.

### Player Feedback (added 2026-05-25)

Multi-field feedback forms. Free for every studio.

- `UPlayloopClient::SubmitFeedback(FormId, Responses, OnDone)` posts to `POST /api/telemetry/feedback`. Multicast Blueprint delegate fires once with `FPlayloopFeedbackResult` carrying `bOk`, `SubmissionId`, `FormId`, `SessionId`, and `ResponseIds`. Validates `FormId`, an existing session, and a non-empty `Responses` array up front; surfaces server failures via `ErrorCode` (`unknown_field`, `no_responses`, `cross_tenant`, etc.).
- `UPlayloopClient::GetDefaultFeedbackFields()` returns the SDK's three-field starter (1-5 rating + two text fields). Use for a code-defined form when you don't want to author one in the dashboard.
- New types in `PlayloopTypes.h`: `EPlayloopFeedbackFieldKind`, `FPlayloopFeedbackField`, `FPlayloopFeedbackResponse`, `FPlayloopFeedbackResult`, `FOnPlayloopFeedbackSubmitted`. All Blueprint-exposed.
- `UPlayloopFeedbackWidget` (UMG, programmatic): builds the form in `RebuildWidget`, so the plugin ships pure C++ with no `.uasset` to maintain. Three integration paths: spawn the default via `CreateDefault(...)`, subclass in UMG for designer styling, or bring your own widget and call `SubmitFeedback` directly. Short-text + long-text fields wire automatically; rating + yes-no fields hook via `SetRatingValue` / `SetYesNoValue` from a Blueprint subclass.
- `PlayloopSDK.Build.cs` adds `UMG`, `Slate`, and `SlateCore` to `PublicDependencyModuleNames`.

### A/B experiments (added 2026-05-29)

Server-resolved A/B variant reads. Reaches parity with the TypeScript SDK's `client.experiments` surface.

- `UPlayloopExperiments` object, reachable via `UPlayloopClient::Experiments()` (`BlueprintCallable` + `BlueprintPure`). `VariantAsync(id, OnResolved)` resolves the player's variant from `GET /api/telemetry/experiments`; an empty string is the Unreal stand-in for `null`.
- Cache contract per the shared spec: lazy fetch on the first `VariantAsync()`, session-scoped cache, exactly one re-fetch per unknown id (then a cached negative), and last-good on network error. `VariantAsync()` never throws.
- Blueprint nodes: **Variant Async** and **Refresh Async** take a resolved-event delegate. C++ callers use the native `FOnPlayloopVariantResolved` / `FOnPlayloopExperimentsRefreshed` delegates.
- Synchronous, cached-only `Variant(id)` for hot paths that know assignments are warm.
- `RefreshAsync()` re-pulls assignments on demand for long-running sessions.
- `PlayloopConfig.bPrefetchExperiments` (+ the Prefetch Experiments setting) eagerly warms assignments at `Start()` instead of lazily. Requires `GameId`.
- The first telemetry flush tags the session with its experiment assignments (`experimentTags`); the server re-validates each tag before recording it.
- `FPlayloopHttpClient::GetJson(...)` adds GET support with the same retry policy as `PostJson`.
- `PlayloopExperimentsSpec.cpp` covers the four cache behaviors with an injected synchronous fetcher.

### Branded settings window (added 2026-06-24)

A themed Slate settings window, matching the Unity and Godot SDK panels (every
SDK is first-class). `Playloop > Settings…` now opens this window instead of
the raw Project Settings details panel.

- New `Source/PlayloopSDKEditor/Private/PlayloopSettingsWindow.{h,cpp}`: dark
  brand palette, the wordmark lockup in the header (`Resources/playloop_wordmark.png`
  via a registered `FSlateStyleSet`), a connection status pill, the connection
  fields (ingest key, base URL, game slug, game id, environment), and the
  auto-instrument toggles (scenes / focus / idle / fps / memory) bound to
  `UPlayloopSettings`. Project Settings, Plugins, Playloop still edits the same
  values.
- Verify Connection resolves the game straight from the ingest key via
  `GET /api/telemetry/resolve` (no slug needed) and reports the resolved game
  name in the pill.
- `PlayloopSDKEditor.Build.cs` adds `HTTP` and `Json` for the Verify request.

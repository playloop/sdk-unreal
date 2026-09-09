# Playloop™ SDK for Unreal

Drop-in telemetry + AI playtest-insights SDK for Unreal 5 games. Stream events, get back structured insights, summaries by tester, and summaries by build, no custom backend needed.

- **Language**: C++ + Blueprint nodes (UFUNCTION wrappers on every public method)
- **Min Engine**: **Unreal 5.3**
- **Targets**: Win64, Mac, Linux, Android, iOS, consoles (HTTP via FHttpModule)
- **Modules**: `PlayloopSDK` (Runtime), `PlayloopSDKEditor` (Editor), `PlayloopSDKTests` (DeveloperTool)

## Install

Clone or copy the plugin into your project's `Plugins/` folder:

```bash
git clone --branch v0.5.0 https://github.com/playloop/sdk-unreal.git YourProject/Plugins/PlayloopSDK
```

Open the project; UE prompts to rebuild. After the rebuild, **Project Settings, Plugins, Playloop** appears in the editor.

This SDK ships as **source** (no prebuilt binaries), so the project must be able to compile C++. Even a Blueprint-only project needs a C++ toolchain to build the plugin once. Every public method has a Blueprint node, so designers never touch C++ afterward.

Each game has its own **Ingest key**. Copy it from that game's setup page on playloop.gg (**Connections, Unreal**, then pick the game), paste it into the **ApiKey** field of the Playloop section of Project Settings, or set it from C++ at runtime. Because the key belongs to a game, telemetry attributes to that game automatically. (Automating onboarding? If the game doesn't exist yet, an agent with the Playloop MCP or a management key can call `create_game` / `POST /api/v1/games` to mint the game and a show-once ingest key.)

> **Use the Ingest key, not the Management key.** The per-game Ingest key is what `UPlayloopClient` uses. The Management key (from Settings, API keys) is a per-account, server-side key. Never ship it in a packaged Unreal build. See [API keys](#api-keys).

### Depend on the module (C++ callers)

Enabling the plugin does **not** by itself put the SDK's headers on your include path. Any C++ file that `#include`s a Playloop header (`PlayloopClient.h`, `PlayloopExperiments.h`, and so on) needs your game module to depend on the `PlayloopSDK` module. In `Source/<YourGame>/<YourGame>.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "PlayloopSDK",   // so #include "PlayloopClient.h" resolves
});
```

(`PrivateDependencyModuleNames` is fine if you only use the SDK in `.cpp` files.) Pure-Blueprint / auto-start integrations don't include any header, so they can skip this.

## First event (auto-start via Project Settings)

Set **ApiKey** in Project Settings, Plugins, Playloop and leave **bAutoStart** on. The ingest key belongs to a game, so the per-game key is all you need: telemetry, experiments, and Tester Keys all attribute to that game automatically. There is nothing else to configure. The plugin's `UPlayloopSubsystem` constructs and starts a client on GameInstance init.

Then anywhere in C++ or Blueprint:

```cpp
#include "PlayloopClient.h"

void AMyCharacter::OnLevelComplete()
{
    if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())
    {
        Pl->Track(TEXT("level_complete"), TEXT("{\"level\":\"act-1\",\"durationSec\":214}"));
    }
}
```

Blueprint equivalent: drag a **Get Current** Playloop Client node, then **Track** with the event name + a JSON string of props.

## First event (manual C++ start)

If you'd rather configure the client yourself (e.g. wait for player login before stamping a build version):

```cpp
UPlayloopClient* Client = NewObject<UPlayloopClient>(GetGameInstance());

FPlayloopConfig Config;
Config.ApiKey       = TEXT("pl_ik_...");           // the game's ingest key; everything attributes to that game
Config.Environment  = TEXT("dev");
Config.BuildVersion = TEXT("0.5.0");

Client->Configure(Config);
Client->Start();   // emits session_start + drains any persisted crashes
```

`Start()` also installs the crash trap (via `FCoreDelegates::OnHandleSystemError`) and queues a flush timer. `Stop()` / `EndSession()` flushes one last time.

**Environment auto-derives from the build.** Leave `Environment` at its default (`dev`, or empty) and the SDK picks it for you: a non-shipping build (the editor, a Debug or Development build) is tagged `dev`, a packaged Shipping build is tagged `production`. Set an explicit value (`production`, `demo`, or your own string) to override.

**Editor + dev-build telemetry is suppressed by default** so playtesting from the editor doesn't pollute your real session data. To send while testing locally, set `Config.bSendInEditor = true` (or the **Send In Editor** checkbox under Project Settings → Plugins → Playloop → Advanced) and pair it with `Environment = "dev"` so the traffic is easy to filter out in the dashboard. A Shipping build always sends.

## Safe to construct (never-raise)

The client is **always safe to construct and call.** If you ship a build with a blank or missing ingest key, nothing throws. `UPlayloopClient::GetCurrent()` returns `nullptr` when no client is configured, so the null-guarded call sites shown throughout this README (`if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())`) simply skip, and any Blueprint node resolves to a harmless no-op. Telemetry drops silently, `VariantAsync` yields the control default, and no heartbeat, auto-instrumentation, or crash trap is ever started. A forgotten key degrades to "no telemetry," never a crash.

There is no `IsEnabled` accessor: the `nullptr` from `GetCurrent()` *is* the disabled signal. Guard on it (as the samples do) whenever you want to branch on whether Playloop is live.

Genuine caller bugs still surface as usual: an empty event name or an empty form id is a mistake at the call site, not a configuration problem.

## Heatmaps

Same contract as the other SDKs. Any event named exactly `player_pos` with `{ x, y, room }` is aggregated server-side. Stream it from a periodic tick at 10-20Hz:

```cpp
void APlayerHeartbeat::Tick(float DeltaSec)
{
    if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())
    {
        const FString Json = FString::Printf(
            TEXT("{\"x\":%f,\"y\":%f,\"room\":\"%s\"}"),
            GetActorLocation().X, GetActorLocation().Y, *CurrentRoomName);
        Pl->Track(TEXT("player_pos"), Json);
    }
}
```

## State accumulator

Anything you stamp via `SetState*` / `IncrementState` ships on the next heartbeat and on `session_summary`. Use it for resolution state (final score, did-they-finish, last room reached), not per-action events.

```cpp
Client->SetStateString(TEXT("final_room"), TEXT("act3-throne"));
Client->SetStateBool(TEXT("act2_completed"), true);
Client->IncrementState(TEXT("kills_total"), 1.0);
```

Blueprint nodes: **Set State Bool / Number / String** and **Increment State**.

**Your snapshots are read as a progression.** Because the accumulator ships on every heartbeat, Playloop reads how your state *changed* over the session (e.g. `souls` 0 → 120k → 998k), not only where it ended. Keep stamping state as the player progresses, not just at the end, and the analysis sees the whole arc rather than a single final number. If your game already fires its own recurring snapshot or end-of-session event under a different name, point Playloop at it in your game's Settings → Events (it auto-detects the common names).

## Crash reports

The crash trap installs on `Start()` and captures any `OnHandleSystemError` event (segfaults, asserts, unhandled exceptions). The crash JSON is persisted to `<ProjectSavedDir>/Playloop/pending-crashes.json` and drained on next session start as a `crash_reported` event.

**Scope.** This hooks the engine's system-error delegate, so it captures more than a managed-only handler does, but it is not a full native minidump and won't cover every native-plugin or third-party-module fault. Delivery is on the next launch, not live, since the crashing process can't send. For complete native crash coverage, pair a dedicated native reporter and set `bEnableCrashHandler = false` to avoid double-reporting.

For managed (caught) crashes, call `ReportCrash()` yourself:

```cpp
try { ... }
catch (const std::exception& E)
{
    if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())
    {
        Pl->ReportCrash(FString(E.what()), TEXT("stack-from-your-tracer"));
    }
}
```

Disable the trap entirely with `bEnableCrashHandler = false` on the config.

## Tester Keys

Pair a player with a tester handle redeemed at `playloop.gg/redeem/<token>`:

```cpp
Client->LinkTester(TEXT("pl_tt_abc123"), {});
// dashboards now show "RedFox42" instead of an opaque device id
```

Each call takes an optional completion delegate (pass `{}` to ignore the
result, or bind one to read the parsed `FPlayloopLinkResult` /
`FPlayloopTesterStatus`). Companion methods: `GetCurrentTester()`,
`UnlinkTester()`. See
[docs/tester-keys/sdk-correlation](https://playloop.gg/docs/tester-keys/sdk-correlation)
for the contract.

## A/B experiments

Run experiments from the Playloop dashboard and read the player's variant in your game. Assignment is decided server-side and is deterministic per device, so every Playloop SDK agrees on which bucket a player lands in. Experiments are scoped to the game your ingest key belongs to, so there is nothing extra to configure.

```cpp
#include "PlayloopExperiments.h"

UPlayloopClient* Pl = UPlayloopClient::GetCurrent();

Pl->Experiments()->VariantAsync(
    TEXT("exp_tutorial_v2"),
    FOnPlayloopVariantResolved::CreateLambda([this](const FString& Variant)
    {
        if (Variant == TEXT("treatment")) ShowNewTutorial();
        else ShowClassicTutorial(); // "control", or "" if not running
    }));
```

`VariantAsync()` is async and **never throws**, so it's safe to call straight from gameplay code. An empty string means "no variant": the experiment is unknown, not running, or the first fetch failed. That's Unreal's stand-in for the `null` the TypeScript SDK returns.

- **Lazy fetch.** Assignments are fetched on the first `VariantAsync()` call, not at `Configure()`. Games that never run an experiment pay zero network cost.
- **Session-scoped cache.** The assignment map is cached for the lifetime of the client, so a player sees a stable variant for the whole playthrough.
- **Unknown experiment.** Calling `VariantAsync()` for an id the SDK hasn't seen triggers exactly one re-fetch (in case you just created the experiment), then caches the answer. A brand-new experiment created mid-session is picked up on that re-fetch.
- **Offline-safe.** If the network is down, a previously-cached variant still answers. If the very first fetch fails, `VariantAsync()` resolves to an empty string.

The variant the player saw is tagged onto the session automatically: the first telemetry flush carries the assignments, and the dashboard's experiment detail page counts each session under its variant. The server re-checks every tag against its own answer before recording it.

**Blueprint.** `Get Experiments` off the client returns the experiments object. **Variant Async** and **Refresh Async** are exposed as `BlueprintCallable` nodes with a resolved-event delegate.

**Synchronous read.** On performance-critical paths where you know assignments are already warm, `Experiments()->Variant(TEXT("exp_id"))` returns the cached key (or an empty string on a cache miss) without a fetch.

**Eager prefetch.** If you gate the very first thing a player sees on a variant, warm the assignments at startup with `PlayloopConfig.bPrefetchExperiments = true` (or the **Prefetch Experiments** checkbox under Project Settings, Plugins, Playloop). Assignments are then fetched at `Start()` instead of on the first `VariantAsync()`.

**Manual refresh.** For long-running sessions, re-pull assignments on demand:

```cpp
Pl->Experiments()->RefreshAsync();
```

**Variant config values.** A variant can carry an optional config payload: a flat map of primitive values (string, number, or bool) you set from the dashboard. Use it to tweak a tuning number, a feature flag, or a label per variant without shipping a new build. Read values through the typed getters on `FPlayloopVariantConfig`, each of which takes a fallback:

```cpp
Pl->Experiments()->GetConfigAsync(
    TEXT("exp_tutorial_v2"),
    FOnPlayloopVariantConfigResolved::CreateLambda(
        [](const FPlayloopVariantConfig& Config)
        {
            const float EnemyDamage = Config.GetFloat(TEXT("enemy_damage"), 10.0f);
            const bool bShowHints = Config.GetBool(TEXT("show_hints"), false);
            const FString Label = Config.GetString(TEXT("cta_label"), TEXT("Start"));
        }));
```

`GetConfigAsync()` is async and never throws. It shares the same lazy fetch and session cache as `VariantAsync()`, and resolves to an empty config (`Num() == 0`) when the experiment isn't running or the assigned variant has no config. On a hot path where the cache is already warm, `GetConfig()` reads it synchronously. Values are primitives only, by design: no arrays and no nested objects.

## Player Feedback

Player Feedback is free for every studio. Both authoring forms on the dashboard AND the runtime call below: no plan-based throttling at the SDK layer.

Define a form in the studio dashboard at `/games/[slug]/feedback`. One form can have multiple fields. Supported field kinds: `Rating1To5`, `ShortText`, `LongText`, `YesNo`. Each field gets a stable id (`q1`, `q2`, ...). Your submission matches answers to fields by that id.

### Code path (logic only)

```cpp
TArray<FPlayloopFeedbackResponse> Responses;
Responses.Add({ TEXT("q1"), TEXT("5") });          // rating-1-5
Responses.Add({ TEXT("q2"), TEXT("Great pacing") }); // short-text

FOnPlayloopFeedbackSubmitted OnDone;
OnDone.AddDynamic(this, &AMyHUD::HandleFeedbackResolved);

Client->SubmitFeedback(TEXT("ff_xyz_from_dashboard"), Responses, OnDone);
```

The delegate fires once with `FPlayloopFeedbackResult` carrying `bOk`, `SubmissionId`, `FormId`, `SessionId`, and `ResponseIds` (one persisted row id per field, in submission order). On failure, `ErrorCode` carries the server's `reason` (`unknown_field`, `no_responses`, `cross_tenant`, `already_submitted`, `session_submission_limit`).

### Spam caps (anti-flood)

Standard feedback forms have two server-side limits per playtest session by default:

1. **By default, one submission per `(session, form)`**: `ErrorCode = "already_submitted"`, HTTP 409.
2. **At most 3 submissions per session across standard forms**: `ErrorCode = "session_submission_limit"`, HTTP 429.

Branch on `Result.ErrorCode` in your delegate handler. If a studio wants to ask the player after every level, design one multi-field form (rating + per-level comments) instead of chaining N separate forms.

### Default widget

`UPlayloopFeedbackWidget` builds a form programmatically in `RebuildWidget`, so the plugin ships pure C++ with no `.uasset` to maintain. Three integration paths:

1. **Spawn at runtime** with `UPlayloopFeedbackWidget::CreateDefault(World, Client, FormId, Fields)`. The widget adds itself to the viewport, awaits the player, and fires `OnFeedbackResolved` on submit / cancel / failure.
2. **Subclass in UMG** to override the visual style. Derive `WBP_PlayloopFeedback` from `UPlayloopFeedbackWidget` in the editor. Short-text and long-text fields wire automatically; for rating + yes/no fields, lay them out in the designer and call `SetRatingValue(FieldIndex, Value)` / `SetYesNoValue(FieldIndex, "yes" or "no")` from your button click handlers.
3. **Bring your own widget** and call `UPlayloopClient::SubmitFeedback(...)` directly. Use this when you've already designed a feedback UI in your project's UI kit.

`UPlayloopClient::GetDefaultFeedbackFields()` returns the SDK's default field set (1-5 rating + `What worked?` short-text + `What didn't?` long-text). Use it when you want a code-defined form instead of a dashboard-defined one.

### When to show the form

Player Feedback is dev-triggered. The SDK never auto-opens the form. Common patterns:

- Pause menu: surface a "Send feedback" button.
- Level complete / boss down: a natural breath moment.
- Back-to-menu handler: capture answers, then call `EndSession()` so the submission flushes with the session boundary.

Do not call `SubmitFeedback` from `FCoreDelegates::OnExit` or other quit paths. The HTTP request may not flush before the process dies.

## Bug Reports

Let a player report a bug from inside the game. A bug report is one `Title` + `Description` (both required) with an optional severity. Playloop stores it, surfaces it in the studio dashboard, and routes it to your connected issue tracker automatically when one is set up. Free for every studio.

### Code path (logic only)

```cpp
FOnPlayloopBugReportSubmittedBP OnDone;
OnDone.BindDynamic(this, &AMyHUD::HandleBugReportResolved);

Client->SubmitBugReport(
    TEXT("Boss music keeps playing after the fight"),
    TEXT("Beat the level-2 boss, went back to the hub, the boss track never stopped."),
    EPlayloopBugSeverity::Medium,
    OnDone);
```

The delegate fires once with `FPlayloopBugReportResult` carrying `bOk`, `BugReportId`, `RoutingStatus` (`pending` / `stored_only` / `routed` / `partial` / `failed`), and `bIdempotent`. On failure, `ErrorCode` explains why (`invalid_argument`, `disabled`, or the server's `reason`).

Build version, environment, and platform are attached automatically. A fresh idempotency key is generated per call, so a transport retry lands the report once. A disabled client (no API key) fails soft: the callback fires with `bOk = false` and no network request.

Need the optional fields (your own `PlayerId`, an explicit `SessionId`, a caller-chosen `IdempotencyKey`, or extra device `Context`)? Use `SubmitBugReportWithContext(Title, Description, Severity, Params, OnDone)` with an `FPlayloopBugReportParams`.

### Default widget

`UPlayloopBugReportWidget` builds the form programmatically in `RebuildWidget`, so the plugin ships pure C++ with no `.uasset` to maintain. It has a Title field, a Description field, a four-option Severity selector (default Medium), and Submit / Cancel. Three integration paths, mirroring the feedback widget:

1. **Spawn at runtime** with `UPlayloopBugReportWidget::CreateDefault(World, Client)`. The widget adds itself to the viewport, awaits the player, and fires `OnBugReportResolved` on submit / cancel / failure.
2. **Subclass in UMG** to override the visual style. Derive `WBP_PlayloopBugReport` from `UPlayloopBugReportWidget` in the editor; call `SetSeverity(...)` from your own severity control if you replace the built-in selector.
3. **Bring your own widget** and call `UPlayloopClient::SubmitBugReport(...)` directly.

### When to show it

Bug reporting is dev-triggered; the SDK never auto-opens the form. Common patterns:

- Pause menu: a "Report a bug" entry, available any time.
- A dedicated key/button that opens `CreateDefault(World, Client)`.
- After a caught, recoverable error, offer the player a prefilled report.

As with feedback, don't submit from `FCoreDelegates::OnExit` or other quit paths: the request may not flush before the process dies.

## Discord ingest

Relay a Discord channel (optionally a thread) into a playtest session so the conversation folds into Playloop alongside your telemetry. The same surface exists in every Playloop SDK; in Unreal it lives on `Discord()`:

```cpp
UPlayloopClient* Client = NewObject<UPlayloopClient>(GetGameInstance());

FPlayloopConfig Config;
Config.ApiKey      = TEXT("pl_mgmt_...");   // management key, server-side only
Config.RelaySecret = TEXT("...");           // from Connections > Discord
Client->Configure(Config);

Client->Discord()->IngestAsync(
    TEXT("123456789012345678"),   // Discord channel id
    TEXT("my-game"),              // game slug or id
    FString(),                    // optional thread id, leave empty to omit
    FString(),                    // per-call relay secret, leave empty to use Config.RelaySecret
    FOnPlayloopDiscordIngested::CreateLambda([](const FPlayloopDiscordIngestResult& Result)
    {
        if (Result.bOk)
        {
            // SessionsCreated is 1 when a new session shell was opened for the
            // channel/thread, 0 when this post appended to an open session.
            UE_LOG(LogTemp, Log, TEXT("Discord ingest ok (created %d)."), Result.SessionsCreated);
        }
    }));
```

The server opens one session per channel/thread window and appends while it stays open. `IngestAsync` never throws. Bad input or a failed request comes back as a result with `bOk = false` and an `ErrorCode` (`invalid_argument`, `http_error`, `parse_error`). A Blueprint-callable overload with the same signature is exposed for BP graphs.

**This endpoint requires a management key, not the ingest key.** It's built for a trusted relay (a bot or server bridge), not a packaged player build. Set `ApiKey` to a `pl_mgmt_...` key in the process that calls `Ingest`, and keep that key off end-user machines (see [API keys](#api-keys)).

**It also requires your Discord relay secret.** Copy it from the Connections > Discord page on playloop.gg (it's shown once when generated; you can rotate it there any time). Set it as the client-level `FPlayloopConfig.RelaySecret`, or per call via `IngestAsync`'s `RelaySecret` parameter if one process relays for more than one account (the per-call value wins). The SDK sends it as the `x-playloop-secret` header on Discord ingest calls only. Like the management key, keep it server-side only: set it in code, not in Project Settings, so it never ships in a player build. Calls without it are rejected.

## Editor tooling

Top-level **Playloop** menu in the editor menu bar (same parity as the Unity SDK). Items:

- **Settings...**: opens Project Settings, Plugins, Playloop.
- **Send Test Event**: fires a `test_event` from the live PIE client (Play in Editor required).
- **Set Environment** submenu: production / demo / dev / **Custom...** (enter your own environment string).
- **Docs** submenu: **Open Docs** and **Open Roadmap**.
- **Open Dashboard for This Game**: opens `playloop.gg/games/<slug>`.
- **Open GitHub Repo**.

Settings written through the menu are persisted to `Config/DefaultPlayloop.ini` (section `[/Script/PlayloopSDK.PlayloopSettings]`), not `DefaultEngine.ini`.

## What you get for free

- **Auto-batch**: events queue in memory and flush every 5s. Configurable via `FlushIntervalMs`. Set to 0 to disable; call `Flush()` yourself.
- **Heartbeat**: a `session_heartbeat` event fires every 60s carrying the current state accumulator snapshot. Used by the server's stale-session sweeper.
- **Retry policy**: transient HTTP failures (network errors, 5xx, 408, 429) retry up to `MaxHttpRetries` times with 500ms-base exponential backoff + jitter (5s cap).
- **Stable device id**: a v4 GUID is generated on first run and persisted to `<ProjectSavedDir>/Playloop/device.id`. Same device collapses to one tester across reinstalls of the same project.
- **Crash trap**: installed automatically; survives force-quits because crash JSON lands on disk before the engine dies.
- **Auto-instrument**: a handful of "useful by default" events fire automatically, no wiring needed (ON by default; memory is opt-in). They route through the normal `Track()` path, so they respect your dashboard event settings and the editor-suppression gate:
  - `focus_lost` / `focus_gained`: when the app loses or regains focus (alt-tab, minimize).
  - `fps_drop {medianFps, lowFps, windowSec}`: when the rolling 3s median frame rate dips below `FpsDropThreshold` (default 30), throttled to one event per 5s.
  - `scene_changed {from, to}`: when the active map or level changes.
  - `idle_start {idleAfterSec}` / `idle_end {idleDurationSec}`: around stretches with no keyboard, mouse, touch, or gamepad input (`IdleThresholdSec`, default 30s desktop / 60s mobile).
  - `memory_pressure {totalMb, allocatedMb}`: opt-in (`bAutoInstrumentMemory = true`); fires near the physical-RAM ceiling, throttled to one per 30s.

  Toggle each arm (and the two thresholds) under **Project Settings → Plugins → Playloop → Auto Instrument**, or on `FPlayloopConfig` for a manual start. Event names and payload fields match the Unity and Godot SDKs exactly.

## Tests

Tests use Unreal's Automation Spec framework (`BEGIN_DEFINE_SPEC`). Run them from the editor or the command line:

```
# From within the editor: Window, Developer Tools, Session Frontend, Automation
# Filter to "Playloop"

# From CLI:
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests Playloop" -TestExit="Automation Test Queue Empty" -ReportOutputPath="Saved/Automation/Reports"
```

Test specs live under `Source/PlayloopSDKTests/Private/`:

- `PlayloopEventQueueSpec.cpp`: Enqueue/Drain, RestoreFront, ShortId alphabet, SnapshotTail, envelope JSON shape.
- `PlayloopCrashHandlerSpec.cpp`: managed crash persist + drain round-trip, oversized-stack truncation.
- `PlayloopClientStateSpec.cpp`: state accumulator behavior.
- `PlayloopDeviceIdSpec.cpp`: stable device id resolution + test override.
- `PlayloopExperimentsSpec.cpp`: A/B variant cache (lazy fetch, session cache, refetch-once on unknown id, last-good on network error).
- `PlayloopDiscordSpec.cpp`: Discord ingest (snake_case body, optional thread_id, channelId/game validation, relay-secret resolution, http_error mapping).

## API keys

Two scopes, minted in different places:

- **Ingest key** (`pl_ik_...`): write-only, scoped to telemetry, and tied to a single game. Copy it from that game's setup page (**Connections, Unreal**, then pick the game). Safe to ship in a packaged build. This is what `UPlayloopClient` uses, and because it belongs to a game, telemetry attributes to that game automatically.
- **Management key** (`pl_mgmt_...`): per-account, full read/write. Minted under [Settings, API keys](https://playloop.gg/settings). Server-only. **Never ship this in an Unreal binary**; it has access to your account's full data.

If you bake a key into the build, bake the Ingest key. Rotate it from the game's setup page if it ever leaks.

## Where to go next

- [Docs](https://playloop.gg/docs): the analysis pipeline, AI prompt tips, account security.
- [Live example](https://playloop.gg/demo): a real Playloop dashboard from a real game.
- [Roadmap](https://playloop.gg/roadmap): what's shipped, what's next.
- [Editor tooling](https://playloop.gg/docs/sdk/editor-tooling): the Unity + Godot + Unreal menu reference.
- [SDK troubleshooting](https://playloop.gg/docs/sdk/troubleshooting): what to do when something looks off.

### Repeat voluntary feedback and safe retries

Forms default to one submission per form/session and three standard-form submissions per session. Enable **Allow repeat voluntary feedback** when creating a form for a player-opened feedback button. That form accepts new notes in the same session without consuming the standard-form allowance. It never schedules prompts.

Pass `RequestId` and `OriginalSessionId` (the final two `SubmitFeedback` arguments) to replay a saved note after restarting. Omit `OriginalSessionId` for a new note in the current session. The SDK generates a fresh ID when omitted and keeps it unchanged during transport retries. Persist the ID with the original session ID, form ID, answers and timestamps until delivery is acknowledged. Cancellation or a lost response does not prove the server rejected the note. Retry the unchanged request to receive its original receipt. Changed answers or timestamps under the same ID return `idempotency_conflict` (409). A new note needs a new ID. Retain the draft on error; only an acknowledged receipt means delivered. Existing rate limits still apply.

# AGENTS.md: Playloop SDK for Unreal

A guide for AI coding agents (Cursor, Claude Code, Copilot, etc.) integrating
the Playloop SDK into an **Unreal Engine 5** game. It mirrors the public README
and is the quick reference for wiring telemetry, A/B experiments, tester linking,
and player-feedback forms into a project.

## What this SDK is

The official Playloop SDK for Unreal 5: a drop-in plugin that streams gameplay
telemetry to Playloop and gets back structured AI playtest insights, per-tester
summaries, and per-build summaries, with no custom backend to maintain.

- **API surface:** `UPlayloopClient` (a `UObject`). Every public method is
  `UFUNCTION(BlueprintCallable)`, so it's reachable from C++ **and** Blueprint.
- **Min engine:** Unreal 5.3. Targets Win64, Mac, Linux, Android, iOS, consoles
  (HTTP via `FHttpModule`).

## Install

Clone or copy the plugin into the project's `Plugins/` folder, then reopen the
project so Unreal rebuilds it:

```bash
git clone --branch v0.5.0 https://github.com/playloop/sdk-unreal.git YourProject/Plugins/PlayloopSDK
```

After the rebuild, a **Playloop** section appears under **Project Settings →
Plugins**.

The SDK ships as **source**, so the project has to compile C++. A Blueprint-only
project still works (every public method has a Blueprint node), but it needs a
C++ toolchain to build the plugin once. That first project-file regen + rebuild
runs on the developer's machine, so a headless agent hands this step to the dev.

**Depend on the module before you write C++.** Enabling the plugin does not put
the SDK's headers on your include path. Any `.cpp`/`.h` that `#include`s a
Playloop header (`PlayloopClient.h`, `PlayloopExperiments.h`, …) needs your game
module to list the `PlayloopSDK` module as a dependency in
`Source/<YourGame>/<YourGame>.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine",
    "PlayloopSDK",   // so #include "PlayloopClient.h" resolves
});
```

Use `PrivateDependencyModuleNames` if the SDK is only used in `.cpp` files.
Pure-Blueprint / auto-start integrations include no header and can skip this.

## Configure

Each game has its own **Ingest key** (`pl_ik_…`). Copy it from that game's setup
page on playloop.gg (**Connections → Unreal**, then pick the game). The ingest
key is write-only, scoped to one game, and **safe to ship** in a packaged build.
Because it belongs to a game, telemetry attributes to that game automatically.

If the game doesn't exist on Playloop yet and you're automating onboarding, an
agent with the Playloop MCP server (or a management key, `pl_mgmt_…`, at
admin/owner role) can create it with the `create_game` tool (or
`POST /api/v1/games` with `{ "name": "...", "genre": "..." }`). It returns the
new game plus a show-once ingest key to paste here. Otherwise ask the dev to
create the game and grab the key from **Connections → Unreal**.

> **Ship the Ingest key, never the Management key.** The Management key
> (`pl_mgmt_…`, from **Settings → API keys**) is a per-account, full read/write
> server-side key. Never bake it into a player build.

**Easiest path: auto-start via Project Settings.** Paste the ingest key into the
**ApiKey** field under Project Settings → Plugins → Playloop and leave
**bAutoStart** on. A GameInstance subsystem constructs and starts the client at
startup; nothing else is required to begin sending telemetry.

**Manual C++ start** (e.g. to stamp a build version or wait for login):

```cpp
#include "PlayloopClient.h"

UPlayloopClient* Client = NewObject<UPlayloopClient>(GetGameInstance());

FPlayloopConfig Config;
Config.ApiKey       = TEXT("pl_ik_...");          // this game's ingest key
Config.GameSlug     = TEXT("my-game");            // optional; used by experiments + tester keys
Config.Environment  = TEXT("dev");                // production / demo / dev / custom
Config.BuildVersion = TEXT("0.5.0");              // surfaced in the dashboard

Client->Configure(Config);
Client->Start();   // emits session_start + drains any persisted crashes
```

### Config options (`FPlayloopConfig`)

| Field | Default | Purpose |
|---|---|---|
| `ApiKey` | - | The game's ingest key (`pl_ik_…`). Required. |
| `GameSlug` | - | Game slug. Optional for telemetry; used by experiments + tester keys. |
| `GameId` | - | Game id (`g_…`). Needed for experiments + tester keys when `GameSlug` isn't set. |
| `Environment` | `dev` | Tag stamped on every session. Left at the default (`dev` / empty), it auto-derives from the build: a non-shipping / editor build stays `dev`, a shipping build becomes `production`. An explicit value wins. |
| `BuildVersion` | - | Build version shown in the dashboard. |
| `ApiBaseUrl` | `https://playloop.gg` | Leave as-is; always points at Playloop. |
| `FlushIntervalMs` | `5000` | Auto-batch cadence. `0` disables; call `Flush()` yourself. |
| `HeartbeatIntervalMs` | `60000` | Heartbeat cadence. `0` disables heartbeats. |
| `bEnableCrashHandler` | `true` | Install the crash trap. |
| `MaxHttpRetries` | `3` | Retries for transient HTTP failures. |
| `bPrefetchExperiments` | `false` | Fetch A/B assignments at `Start()` instead of lazily. |
| `bSendInEditor` | `false` | Send telemetry from the editor (PIE) + non-shipping (Debug / Development) builds. OFF suppresses those sends so they don't pollute real data (a one-time warning logs how to opt in). Pair with `Environment = "dev"`. No effect in a shipping build. |
| `bAutoInstrumentScenes` | `true` | Auto-emit `scene_changed {from,to}` on map/level change. |
| `bAutoInstrumentFocus` | `true` | Auto-emit `focus_lost` / `focus_gained` on app focus change. |
| `bAutoInstrumentIdle` | `true` | Auto-emit `idle_start {idleAfterSec}` / `idle_end {idleDurationSec}` around no-input stretches. |
| `bAutoInstrumentFps` | `true` | Auto-emit `fps_drop {medianFps,lowFps,windowSec}` when the rolling median FPS dips. |
| `bAutoInstrumentMemory` | `false` | Auto-emit `memory_pressure {totalMb,allocatedMb}` near the RAM ceiling. Opt-in (noisy). |
| `IdleThresholdSec` | `0` | Idle seconds before `idle_start`. `0` → default (30 desktop / 60 mobile). |
| `FpsDropThreshold` | `30` | Median FPS (over ~3s) below this fires `fps_drop`. |

All auto-instrument events route through `Track()`, so they honor dashboard event settings (`sdkIgnore` / `linkToSummary`) and the editor-suppression gate. Names + payload fields match the Unity and Godot SDKs exactly.

## Core API

`UPlayloopClient` is the single entry point. Get the running instance anywhere
with `UPlayloopClient::GetCurrent()`.

### Lifecycle

```cpp
Client->Start();        // emit session_start, install timers (idempotent)
Client->Flush();        // force-flush pending events now
Client->EndSession();   // emit session_summary, flush, then stop
Client->Stop();         // stop timers + flush + clear session (idempotent)
```

### Telemetry

```cpp
// Blueprint-friendly: props are a JSON string ("{}" for none)
Client->Track(TEXT("level_complete"), TEXT("{\"level\":\"act-1\",\"durationSec\":214}"));

// C++ structured form
Client->TrackJson(TEXT("level_complete"), MyFJsonObjectPtr);
```

Blueprint: drag a **Get Current** Playloop Client node, then **Track** with the
event name and a JSON string of props.

### State accumulator

Anything you stamp via `SetState*` / `IncrementState` rides along on the next
heartbeat and on `session_summary`. Use it for resolution state (final score,
did-they-finish, last room reached), not per-action events.

```cpp
Client->SetStateString(TEXT("final_room"), TEXT("act3-throne"));
Client->SetStateBool(TEXT("act2_completed"), true);
Client->SetStateNumber(TEXT("score"), 4200);
Client->IncrementState(TEXT("kills_total"), 1.0);
Client->ClearState();
```

Because the accumulator ships on every heartbeat, Playloop reads how your state
*changed* over the session, not only where it ended. Keep stamping state as the
player progresses and the analysis sees the whole arc. Blueprint nodes: **Set
State Bool / Number / String** and **Increment State**.

### Heatmaps

Any event named exactly `player_pos` with `{ x, y, room }` is aggregated into
per-room heatmaps server-side. Stream it from a periodic tick at 10–20 Hz:

```cpp
const FString Json = FString::Printf(
    TEXT("{\"x\":%f,\"y\":%f,\"room\":\"%s\"}"),
    Loc.X, Loc.Y, *CurrentRoomName);
Client->Track(TEXT("player_pos"), Json);
```

`x` / `y` are finite world-space numbers; `room` is a non-empty stable string.
Events missing a field are dropped server-side. Same shape across every Playloop SDK.

### Crash reports

The crash trap installs on `Start()` and captures engine system-error events
(segfaults, asserts, unhandled exceptions). The report is persisted to disk and
sent on the next launch (the crashing process can't send live). For a caught
exception you can also report manually:

```cpp
Client->ReportCrash(FString(E.what()), TEXT("stack-from-your-tracer"));
```

The trap captures more than a managed-only handler, but it isn't a full native
minidump and won't cover every native-plugin fault. For complete native coverage,
pair a dedicated native reporter and set `bEnableCrashHandler = false` to avoid
double-reporting.

## A/B experiments

Run experiments from the Playloop dashboard and read the player's variant in your
game. Assignment is decided server-side and is deterministic per device, so every
SDK agrees on which bucket a player lands in. Set `GameId` (from the game's
Settings) since experiments are scoped to a game.

```cpp
#include "PlayloopExperiments.h"

UPlayloopClient::GetCurrent()->Experiments()->VariantAsync(
    TEXT("exp_tutorial_v2"),
    FOnPlayloopVariantResolved::CreateLambda([this](const FString& Variant)
    {
        if (Variant == TEXT("treatment")) ShowNewTutorial();
        else ShowClassicTutorial(); // "control", or "" if not running
    }));
```

`VariantAsync()` is async and **never throws**. Safe to call from gameplay code.
An empty string means "no variant" (unknown, not running, or first fetch failed).

- **Lazy fetch:** assignments fetch on the first `VariantAsync()` call, so games
  that never run an experiment pay zero network cost.
- **Session-scoped cache:** a player sees a stable variant for the whole session.
- **Eager prefetch:** set `bPrefetchExperiments = true` (or the **Prefetch
  Experiments** checkbox) when the first thing a player sees is gated on a variant.
- **Synchronous read:** `Experiments()->Variant(TEXT("exp_id"))` returns the
  cached key (or empty) without a fetch, for hot paths where assignments are warm.
- **Manual refresh:** `Experiments()->RefreshAsync()` for long sessions.

The variant a player saw is tagged onto the session automatically. Without
`GameId` set, `VariantAsync()` always resolves to an empty string.

Blueprint: **Get Experiments** off the client, then **Variant Async** /
**Refresh Async** nodes with a resolved-event delegate.

## Tester linking

Pair a player with a friendly tester handle redeemed at
`playloop.gg/redeem/<token>`, so dashboards show "RedFox42" instead of an opaque
device id:

```cpp
Client->LinkTester(TEXT("pl_tt_abc123"), {});
```

Each call takes an optional completion delegate: pass `{}` to ignore the result,
or bind one to read the parsed result struct. Companion methods:

```cpp
Client->GetCurrentTester(OnStatus);   // read the current tester, if any
Client->UnlinkTester(TEXT("pl_tt_abc123"), OnDone);  // pass the same token used to link
```

`LinkTester` resolves with `FPlayloopLinkResult` (`bOk`, `bAlreadyLinked`,
`Handle`, `ErrorCode`); `GetCurrentTester` with `FPlayloopTesterStatus`
(`bLinked`, `Handle`); `UnlinkTester` with `FPlayloopUnlinkResult`. All three are
also Blueprint-callable.

## Player feedback forms

Player Feedback is free for every studio. Define a form in the dashboard at
`/games/[slug]/feedback`; one form can have multiple fields. Supported field
kinds: `Rating1To5`, `ShortText`, `LongText`, `YesNo`. Each field has a stable id
(`q1`, `q2`, …); your submission matches answers to fields by that id.

```cpp
TArray<FPlayloopFeedbackResponse> Responses;
Responses.Add({ TEXT("q1"), TEXT("5") });            // Rating1To5
Responses.Add({ TEXT("q2"), TEXT("Great pacing") }); // ShortText

FOnPlayloopFeedbackSubmitted OnDone;
OnDone.AddDynamic(this, &AMyHUD::HandleFeedbackResolved);

Client->SubmitFeedback(TEXT("ff_xyz_from_dashboard"), Responses, OnDone);
```

The delegate fires once with `FPlayloopFeedbackResult` carrying `bOk`,
`SubmissionId`, `FormId`, `SessionId`, and `ResponseIds` (one row id per field, in
submission order). On failure, `ErrorCode` carries the server reason
(`unknown_field`, `no_responses`, `already_submitted`, `session_submission_limit`).

**Anti-flood caps (per playtest session):** one submission per `(session, form)`
(`already_submitted`, HTTP 409) and at most 3 submissions per session across standard
forms (`session_submission_limit`, HTTP 429). To ask after every level, design
one multi-field form instead of chaining several.

**UI options:**

1. **Spawn the default widget** with
   `UPlayloopFeedbackWidget::CreateDefault(World, Client, FormId, Fields)`. It
   adds itself to the viewport and fires `OnFeedbackResolved` on submit / cancel.
2. **Subclass in UMG** (derive `WBP_PlayloopFeedback`) to restyle; for rating /
   yes-no fields call `SetRatingValue` / `SetYesNoValue` from your button handlers.
3. **Bring your own UI** and call `Client->SubmitFeedback(...)` directly.

`UPlayloopClient::GetDefaultFeedbackFields()` returns the SDK's default field set
(1–5 rating + "What worked?" short-text + "What didn't?" long-text) when you'd
rather use a code-defined form than a dashboard one.

**When to show it:** feedback is dev-triggered. The SDK never auto-opens the
form. Good moments: a pause-menu "Send feedback" button, level-complete / boss-down,
or a back-to-menu handler (then call `EndSession()` so the submission flushes with
the session boundary). Don't submit from a quit/exit path; the request may not
flush before the process dies.

## Player bug reports

Let a player report a bug in-game. Free for every studio. One `Title` +
`Description` (both required) and an optional severity; Playloop stores the
report, shows it in the dashboard, and routes it to a connected issue tracker
automatically.

```cpp
FOnPlayloopBugReportSubmittedBP OnDone;
OnDone.BindDynamic(this, &AMyHUD::HandleBugReportResolved);

Client->SubmitBugReport(
    TEXT("Boss music keeps playing after the fight"),
    TEXT("Beat the level-2 boss, went back to the hub, the track never stopped."),
    EPlayloopBugSeverity::Medium,
    OnDone);
```

The delegate fires once with `FPlayloopBugReportResult` (`bOk`, `BugReportId`,
`RoutingStatus`, `bIdempotent`; `ErrorCode` on failure). Build version /
environment / platform are attached automatically, and a per-call idempotency
key makes a transport retry land once. A disabled client (no API key) fails soft
(`bOk = false`, no network). Optional fields (player id, explicit session,
idempotency key, device context) live on `SubmitBugReportWithContext(...)` via
`FPlayloopBugReportParams`.

**Default widget:** `UPlayloopBugReportWidget` builds a Title + Description +
Severity form in `RebuildWidget` (no `.uasset`). Spawn it with
`UPlayloopBugReportWidget::CreateDefault(World, Client)`, subclass
`WBP_PlayloopBugReport` to restyle, or bring your own UI and call
`SubmitBugReport(...)` directly. Show it from a pause-menu "Report a bug" entry.

## What you get for free

- **Auto-batch:** events queue in memory and flush every 5s (configurable;
  `0` disables and you call `Flush()`).
- **Heartbeat:** a `session_heartbeat` carrying the current state snapshot fires
  every 60s.
- **Retry policy:** transient HTTP failures (network, 5xx, 408, 429) retry with
  exponential backoff + jitter, up to `MaxHttpRetries`.
- **Stable device id:** generated on first run and persisted, so the same device
  collapses to one tester across reinstalls.
- **Crash trap:** installed automatically; survives force-quits because the crash
  report lands on disk before the engine dies.

## Editor tooling

A top-level **Playloop** menu in the editor: **Settings…**, **Send Test Event**
(fires from the live PIE client), a **Set Environment** submenu, **Docs**, **Open
Dashboard for This Game**, and **Open GitHub Repo**. Settings written through the
menu persist to `Config/DefaultPlayloop.ini`.

## Using this with your coding agent

When wiring this SDK into a project, an agent should:

1. **Place the plugin** under `YourProject/Plugins/PlayloopSDK` and let Unreal
   rebuild it; confirm **Project Settings → Plugins → Playloop** appears. If you
   write any C++ against the SDK, add `"PlayloopSDK"` to your game module's
   `.Build.cs` dependencies first, or the `#include` won't compile.
2. **Set the ingest key** (`pl_ik_…`) in Project Settings (which writes
   `Config/DefaultPlayloop.ini`), or in a `FPlayloopConfig` for a manual start.
   Never hardcode a Management key into game code.
3. **Pick a start path:** leave `bAutoStart` on for zero-code telemetry, or call
   `Configure()` + `Start()` from a GameInstance / early gameplay hook.
4. **Instrument meaningful moments** with `Track`, and stamp resolution state with
   `SetState*` / `IncrementState` as the player progresses. Use the exact
   `player_pos` `{ x, y, room }` shape for heatmaps.
5. **Add a feedback entry point** (pause menu and/or end-of-run) using a
   dashboard-defined form id, since feedback is dev-triggered.
6. **End sessions deliberately** with `EndSession()` on a return-to-menu or
   level-complete boundary so the summary flushes.

Always capture a `UPlayloopClient*` in a long-lived lambda via a weak reference
(`TWeakObjectPtr`), since Unreal garbage-collects `UObject`s.

## Where to go next

- [Docs](https://playloop.gg/docs)
- [Live example dashboard](https://playloop.gg/demo)
- [Roadmap](https://playloop.gg/roadmap)

### Repeat voluntary feedback and safe retries

Forms default to one submission per form/session and three standard-form submissions per session. Enable **Allow repeat voluntary feedback** when creating a form for a player-opened feedback button. That form accepts new notes in the same session without consuming the standard-form allowance. It never schedules prompts.

Pass `RequestId` and `OriginalSessionId` (the final two `SubmitFeedback` arguments) to replay a saved note after restarting. Omit `OriginalSessionId` for a new note in the current session. The SDK generates a fresh ID when omitted and keeps it unchanged during transport retries. Persist the ID with the original session ID, form ID, answers and timestamps until delivery is acknowledged. Cancellation or a lost response does not prove the server rejected the note. Retry the unchanged request to receive its original receipt. Changed answers or timestamps under the same ID return `idempotency_conflict` (409). A new note needs a new ID. Retain the draft on error; only an acknowledged receipt means delivered. Existing rate limits still apply.

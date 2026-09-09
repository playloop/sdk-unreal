# Crash reporting

The Playloop Unreal SDK installs a crash trap at module load
(`FPlayloopSDKModule::StartupModule`) via
`FCoreDelegates::OnHandleSystemError`. When the engine catches an OS
level crash (segfault, assert, unhandled exception), the trap writes a
JSON record to `<ProjectSavedDir>/Playloop/pending-crashes.json` before
the engine dies. The next time the client starts, those records drain
as `crash_reported` events.

This file documents the moving parts so you can decide what to wire in
your project.

## What gets captured automatically

When `bEnableCrashHandler` is on (the default), every fatal crash
captures:

- The active session id (or empty if no `session_start` had been sent).
- `BuildVersion` from your config.
- `SdkVersion` constant (`playloop-unreal@<version>`).
- `Platform` string from `PLATFORM_*` macros.
- The current state accumulator snapshot.
- The most recent N events from the queue tail
  (`CrashRecentEventsCount`, default 5).
- A stack trace from `FPlatformStackWalk::StackWalkAndDump`, capped at
  12KB.

The file itself is capped at 64KB total. Oldest crashes drop first.

## Managed crashes (caught exceptions)

`OnHandleSystemError` only fires for OS level crashes. For caught
exceptions in your gameplay code, call `ReportCrash` yourself:

```cpp
#include "PlayloopClient.h"

try
{
    DoTheRiskyThing();
}
catch (const std::runtime_error& E)
{
    if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())
    {
        Pl->ReportCrash(FString(E.what()), TEXT("RiskyThing::Run"));
    }
    // re-raise / recover / etc.
}
```

Same persistence + drain flow as the automatic trap.

## Disabling the trap

Two ways:

```cpp
// Per-client, before Start():
FPlayloopConfig Config;
Config.bEnableCrashHandler = false;
Client->Configure(Config);
```

```ini
; In Config/DefaultPlayloop.ini, applies to the auto-start path:
[/Script/PlayloopSDK.PlayloopSettings]
bEnableCrashHandler=False
```

With the flag off:

- The crash delegate stays registered at module level (cannot be
  uninstalled mid-session safely), but the handler short circuits
  without writing.
- `Start()` does NOT drain the pending file.
- `ReportCrash` returns early without persisting.

The trap is harmless when disabled, it just never produces output.

## Tuning the recent-events tail

```cpp
Config.CrashRecentEventsCount = 0;   // omit recentEvents entirely
Config.CrashRecentEventsCount = 50;  // cap; the tail buffer holds 50
```

A larger tail makes attribution easier when crashes only manifest
several events into a session. The trade-off is JSON file size and
network cost on the next drain.

## Where to inspect

- **Local file**:
  `<ProjectSavedDir>/Playloop/pending-crashes.json`. Pretty-print
  with `jq` to read. It is a JSON array of crash objects.
- **Dashboard**: after a successful drain, crash events appear under
  the game's Crashes tab and group by stack signature, so a recurring
  bug shows once with a count.
- **Slack / Discord**: configure the per-game crash notifications
  under Settings, Crash notifications. First occurrence of a new
  signature pings; deduped over 24 hours.

## Symbolication

Unreal builds ship native binaries; stack frames look like
`Module!Function+0x123`. For shipping builds you can:

1. Cook with debug symbols enabled (`bUseDebugCRT=true` in the build
   config) so symbols are local.
2. Ship the `.pdb` (Windows) / `.dSYM` (macOS) / `.sym` (Linux) bundle
   to a private store and run `atos` / `dump_syms` against incoming
   crash reports.

The dashboard does not currently auto-symbolicate Unreal stacks the way
it does TypeScript source maps. The raw frames are still useful for
signature deduplication.

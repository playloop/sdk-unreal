# Set up Playloop in this Unreal project

You are an AI coding agent. The user has asked you to install and configure **Playloop**, a player-behavior telemetry SDK, into the Unreal project you're currently working in.

Follow the steps below in order. Skip any step that's already done.

## Prerequisites

- Unreal Engine **5.3** or newer. The SDK uses modern `UToolMenus`, `UDeveloperSettings`, and `FCoreDelegates::OnHandleSystemError`.
- **The project must be able to compile C++, even if it's a Blueprint-only project.** This SDK ships as source (no prebuilt binaries), so Unreal has to build the plugin's `PlayloopSDK` module once. Every public method has a `UFUNCTION(BlueprintCallable)` wrapper, so a designer never has to touch C++ afterward, but the first build needs a C++ toolchain (Visual Studio on Windows, Xcode on Mac). A pure Blueprint project with no C++ toolchain installed will fail to compile the plugin. This is the one step an agent cannot do fully headless: generating project files and rebuilding runs on the developer's machine.
- You need this game's **ingest key** (starts with `pl_ik_`). Get it, in order of preference:
  1. **Given to you**: if the user already pasted a `pl_ik_...` key, use it.
  2. **Create the game programmatically**: if the game doesn't exist on Playloop yet and you have the Playloop MCP server (or a management key, `pl_mgmt_...`), call the `create_game` tool (or `POST /api/v1/games` with `{ "name": "...", "genre": "..." }`). It returns the new game plus a **show-once ingest key** to use here. Requires an admin/owner role on the workspace.
  3. **Ask the dev**: otherwise ask the user to open the game on playloop.gg, go to **Connections → Unreal**, pick the game, and copy its ingest key.
  Each game has its own ingest key, and the key identifies the game, so telemetry attributes to it automatically.

## Step 1. Drop the plugin into the project

The SDK ships as an Unreal plugin. Clone the latest `playloop-sdk-unreal` into the project's `Plugins/` folder so the path becomes `<ProjectRoot>/Plugins/PlayloopSDK/PlayloopSDK.uplugin`.

```bash
git clone --branch v0.5.0 https://github.com/playloop/sdk-unreal.git <ProjectRoot>/Plugins/PlayloopSDK
```

If `<ProjectRoot>/Plugins/PlayloopSDK/` already exists, skip this step.

## Step 2. Regenerate project files + rebuild

The user must regenerate IDE project files so Unreal picks up the new modules:

- **Windows**: right-click the `.uproject`, choose **Generate Visual Studio project files**.
- **Mac**: right-click the `.uproject`, choose **Services, Generate Xcode Project**.
- **CLI**: `<UE_PATH>/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh <ProjectRoot>/<Project>.uproject` (substitute the platform script).

Then open the project in Unreal Editor. Unreal will prompt to rebuild the missing modules. Click **Yes**.

If you can't run the editor (you're a headless agent), instruct the user to do this step and continue once they confirm.

## Step 3. Store the key

The settings class is `UCLASS(Config = Playloop)`, so it reads and writes **`Config/DefaultPlayloop.ini`**, not `DefaultGame.ini` and not `DefaultEngine.ini`. Putting the key anywhere else silently does nothing (the setting stays blank and no telemetry is sent).

Add the key to the project's `Config/DefaultPlayloop.ini` under a `[/Script/PlayloopSDK.PlayloopSettings]` section (see Step 4 for the exact block), OR have the user paste it into **Project Settings, Plugins, Playloop, Api Key** in the editor (preferred for keeping the value out of source control; the editor writes the same `DefaultPlayloop.ini`).

If you persist the key in `Config/DefaultPlayloop.ini`, make sure that file is added to `.gitignore` so the ingest key never lands in source.

## Step 4. Configure the plugin

Open **Project Settings, Plugins, Playloop** and fill in:

- **Api Key**: the game's `pl_ik_...` ingest key. The key belongs to a game, so telemetry attributes to that game automatically; it's all that's needed to send telemetry.
- **Game Slug**: optional. The URL slug from `playloop.gg/games/<slug>`. Only used by A/B experiments and Tester Keys (Game Id covers the same role), not for telemetry.
- **Environment**: `dev` (default), `production`, `demo`, or `custom`.
- Leave **bAutoStart** on so the plugin's `UPlayloopSubsystem` (a `UGameInstanceSubsystem`) boots the client automatically.

If you can't run the editor, edit `Config/DefaultPlayloop.ini`:

```ini
[/Script/PlayloopSDK.PlayloopSettings]
ApiKey=pl_ik_xxx_paste_the_games_ingest_key_here
; GameSlug is optional (A/B experiments + Tester Keys only); telemetry works with just the key.
;GameSlug=their-game-slug
Environment=dev
bAutoStart=True
```

## Step 5. Make your game module depend on the SDK (C++ callers only)

**Any C++ file that `#include`s a Playloop header will fail to compile until your game module depends on the `PlayloopSDK` module.** Enabling the plugin is not enough. Unreal only puts the SDK's public headers on your include path once your module lists it as a dependency.

Open your game module's build rules (`Source/<YourGame>/<YourGame>.Build.cs`) and add `"PlayloopSDK"` to the dependency list:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore",
    "PlayloopSDK",   // <-- add this so #include "PlayloopClient.h" resolves
});
```

`PrivateDependencyModuleNames` works too if you only use the SDK in `.cpp` files. The module name is exactly `PlayloopSDK` (the runtime module in `Source/PlayloopSDK/`).

**Pure Blueprint / auto-start users can skip this step:** you only need the dependency if you write C++ that includes a Playloop header. If you added `"PlayloopSDK"`, regenerate project files so the compiler picks up the change.

## Step 6. Track your first event

Pick one existing place in the user's gameplay code where something interesting happens (level complete, item purchased, boss defeated) and add:

```cpp
#include "PlayloopClient.h"

if (UPlayloopClient* Pl = UPlayloopClient::GetCurrent())
{
    Pl->Track(TEXT("level_completed"), TEXT("{\"level\":3,\"deaths\":0}"));
}
```

Blueprint equivalent: drag a **Get Current** Playloop Client node, then **Track**. Pass `{}` for `Json Props` if no payload.

## Step 7. Verify

**Editor / PIE sends are suppressed by default, on purpose,** so playtesting from the editor doesn't pollute real session data. That means a plain PIE run will start the client but send **nothing** to the dashboard: a `session_start` will *not* appear on playloop.gg, and that is expected, not a bug. To actually verify from the editor, turn sending on first:

- Set **Send In Editor** ON under **Project Settings → Plugins → Playloop → Advanced** (or `bSendInEditor=True` in `Config/DefaultPlayloop.ini`, or `FPlayloopConfig::bSendInEditor = true` for a manual start), and set **Environment = `dev`** so the test traffic is easy to filter out in the dashboard.

Then play the game in PIE for at least 10 seconds and check https://playloop.gg/sessions, and the first session should appear within a minute. (A packaged **Shipping** build always sends regardless of this flag, so verifying from a real build needs no changes.)

In the editor's Output Log, filter to `LogPlayloop`. On a fresh start you should see:

```
LogPlayloop: Playloop SDK starting (PreDefault).
LogPlayloop: Crash trap installed.
LogPlayloop: Playloop client started (game=<slug>, env=dev).
```

If **Send In Editor** is OFF you'll also see a one-time warning that telemetry is suppressed in the editor / non-shipping build and how to opt in. That is the gate above, working as intended.

If the Output Log shows `Start() called without an API key`, the value didn't make it to the settings asset; recheck Steps 3 and 4 (confirm the key is in `Config/DefaultPlayloop.ini`, not `DefaultGame.ini`).

## Things you must not do

- **Do not hard-code the ingest key in C++.** Always read it from Project Settings or a `Config/Default*.ini` file the user adds to `.gitignore`.
- **Do not ship the Management key** (starts with `pl_mgmt_`). Those are admin keys for server-side scripts, never an Unreal binary.
- **Do not commit `Config/DefaultPlayloop.ini` if it contains an API key.** Add it to `.gitignore` or move the key to a non-checked-in environment.
- **Do not change the base URL; it always points at `https://playloop.gg`.**
- **Do not capture `UPlayloopClient*` raw in lambdas that outlive a frame.** Use `TWeakObjectPtr<UPlayloopClient>` + `IsValid()` checks. The SDK's own `Flush()` callback is the reference pattern.

## When you are done

Tell the user, verbatim:

> Playloop is installed. Editor playtests don't send by default (so they don't pollute your real data). Flip **Send In Editor** ON under Project Settings → Plugins → Playloop → Advanced and set Environment to `dev`, then play in PIE for a few seconds and check https://playloop.gg/sessions. Your first session should appear within a minute. Packaged Shipping builds always send, no flag needed.

## If you get stuck

Full SDK reference, gotchas, and the public API surface live in `AGENTS.md` at the repo root: https://github.com/playloop/sdk-unreal/blob/main/AGENTS.md. Read that if a Step above raises questions you can't resolve from this file alone.

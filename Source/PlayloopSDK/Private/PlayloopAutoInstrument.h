#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
// UE 5.8 IWYU: these were previously transitive. FJsonObject is used as a
// type here; UWorld appears in a nested-name-specifier (needs the full type).
#include "Dom/JsonObject.h"
#include "Engine/World.h"

class FPlayloopAutoInstrumentEngine;
class UWorld;

/**
 * Immutable snapshot of the auto-instrumentation knobs captured when the
 * driver starts. Mirrors Unity's AutoInstrumentSettings + Godot's options
 * dict. `Errors` is intentionally absent. Unreal already ships a dedicated
 * crash handler (FCoreDelegates::OnHandleSystemError), so the auto-instrument
 * suite doesn't duplicate an `application_error` arm.
 */
struct FPlayloopAutoInstrumentSettings
{
    bool bScenes = true;
    bool bFocus = true;
    bool bIdle = true;
    bool bFps = true;
    bool bMemory = false;
    double IdleThresholdSec = 0.0;   // 0 → resolve default (30 desktop / 60 mobile)
    double FpsDropThreshold = 30.0;

    /** True iff any individual auto-event would fire. */
    bool AnyEnabled() const { return bScenes || bFocus || bIdle || bFps || bMemory; }
};

/**
 * Driver that auto-emits the "useful by default" telemetry events the SDK
 * promises: focus changes, scene changes, idle transitions, FPS drops, and
 * (opt-in) memory pressure. Started by UPlayloopClient::Start() and stopped on
 * Stop()/EndSession(). A consumer who starts the client gets all of this for
 * free.
 *
 * The pure rate-limit + threshold logic lives in
 * FPlayloopAutoInstrumentEngine so it's unit-testable headless. This class is
 * the glue that pumps engine state (FApp::GetDeltaTime, world/level changes,
 * Slate last-interaction time, FPlatformMemory) into the kernel each frame and
 * dispatches the resulting events through the supplied Track sink: the SAME
 * UPlayloopClient::TrackJson path, so the events respect sdkIgnore + the
 * editor-suppression gate.
 *
 * Cross-engine parity: event names (focus_lost / focus_gained / fps_drop /
 * scene_changed / idle_start / idle_end / memory_pressure) and payload field
 * names match the Unity + Godot SDKs exactly.
 */
class FPlayloopAutoInstrument
{
public:
    /** Track sink: (eventName, dataObject). Invoked on the game thread. */
    DECLARE_DELEGATE_TwoParams(FTrackSink, const FString& /*EventName*/, TSharedPtr<FJsonObject> /*Data*/);

    explicit FPlayloopAutoInstrument(FTrackSink InTrack);
    ~FPlayloopAutoInstrument();

    /** Install delegates + the per-frame ticker. Idempotent. */
    void Start(const FPlayloopAutoInstrumentSettings& InSettings);

    /** Remove delegates + the ticker. Idempotent; safe in any teardown order. */
    void Stop();

private:
    bool TickFrame(float DeltaTime);

    void OnApplicationActivationChanged(bool bIsActive);
    void OnPostWorldInit(UWorld* World, const UWorld::InitializationValues IVS);

    // Per-frame input poll for idle detection: reads Slate's last-user-
    // interaction time and treats a forward jump as "input happened".
    bool DetectInputThisFrame();

    static FString CurrentSceneName();
    static double NowSec();

    FTrackSink Track;
    FPlayloopAutoInstrumentSettings Settings;
    TUniquePtr<FPlayloopAutoInstrumentEngine> Engine;

    bool bStarted = false;

    FTSTicker::FDelegateHandle TickerHandle;
    FDelegateHandle PostWorldInitHandle;

    // scene-change tracking: snapshot the current scene name each tick.
    FString LastSceneName;
    bool bSceneBaselineSet = false;

    // idle input edge: Slate's last-interaction timestamp from the prior tick.
    double LastInteractionSeen = 0.0;
    bool bInteractionBaselineSet = false;

    // focus de-dupe: only fire on an actual transition.
    bool bHasFocus = true;
};

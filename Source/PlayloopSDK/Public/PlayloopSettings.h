#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlayloopSettings.generated.h"

/**
 * Persisted in DefaultPlayloop.ini. Surfaces as
 * Project Settings -> Plugins -> Playloop in the editor.
 */
UCLASS(Config = Playloop, DefaultConfig, meta = (DisplayName = "Playloop"))
class PLAYLOOPSDK_API UPlayloopSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPlayloopSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    /**
     * Playloop ingest key (Ingest scope) for this game. Each game has its own
     * ingest key. Copy it from that game's setup page on playloop.gg
     * (Connections -> Unreal, pick the game). The key identifies the game, so
     * every feature (telemetry, experiments, Tester Keys) attributes to it
     * automatically. This is the only setting you need.
     */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop")
    FString ApiKey;

    /** production / demo / dev / custom. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop")
    FString Environment = TEXT("dev");

    /** Optional override for the custom environment label. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop")
    FString CustomEnvironmentOverride;

    /** Override the build version (default: Project Settings -> Project Version). */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop")
    FString BuildVersionOverride;

    /** Base URL for the Playloop API. Override for local development/testing only. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    FString ApiBaseUrl = TEXT("https://playloop.gg");

    /** Auto-flush cadence in ms. 0 disables auto-batch. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    int32 FlushIntervalMs = 5000;

    /** Heartbeat cadence in ms. 0 disables heartbeats. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    int32 HeartbeatIntervalMs = 60000;

    /** Install the crash trap. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    bool bEnableCrashHandler = true;

    /** Max events captured into a crash report's recentEvents tail. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    int32 CrashRecentEventsCount = 5;

    /** Eagerly fetch A/B experiment assignments at start instead of lazily. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    bool bPrefetchExperiments = false;

    /** Auto-construct and start a UPlayloopClient on GameInstance init. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop")
    bool bAutoStart = true;

    /**
     * Send telemetry from the editor (PIE) and non-shipping (Debug /
     * Development) builds. OFF by default. Editor playtesting is suppressed so
     * it doesn't pollute real session data (a one-time warning logs how to opt
     * in). Turn ON to test the ingest pipeline locally; pair with Environment =
     * "dev" so the traffic is easy to filter out. No effect in a Shipping build.
     */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|Advanced")
    bool bSendInEditor = false;

    // ── Auto-instrument: "useful by default" telemetry emitted automatically.
    // On by default (memory opt-in). Names + payloads match Unity / Godot.

    /** Emit `scene_changed` on map/level change. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentScenes = true;

    /** Emit `focus_lost` / `focus_gained` on app focus change. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentFocus = true;

    /** Emit `idle_start` / `idle_end` around no-input stretches. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentIdle = true;

    /** Emit `fps_drop` when the rolling median FPS dips below the threshold. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentFps = true;

    /** Emit `memory_pressure` near the physical-RAM ceiling. Opt-in (noisy). */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentMemory = false;

    /** Idle threshold (sec) before `idle_start`. 0 → default (30 desktop / 60 mobile). */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    float IdleThresholdSec = 0.f;

    /** Median FPS below this (over ~3s) fires `fps_drop`. */
    UPROPERTY(Config, EditAnywhere, Category = "Playloop|AutoInstrument")
    float FpsDropThreshold = 30.f;

    /** Resolves Environment + CustomEnvironmentOverride to the value stamped on session_start. */
    FString ResolvedEnvironment() const;
};

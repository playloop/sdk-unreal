#pragma once

#include "CoreMinimal.h"

/** Result of one RecordFrame() call: whether a fps_drop should fire + its payload. */
struct FPlayloopFpsDropDecision
{
    bool bFire = false;
    double MedianFps = 0.0;
    double LowFps = 0.0;
    double WindowSec = 0.0;
};

/** Result of one Tick() of the idle state machine. */
struct FPlayloopIdleDecision
{
    enum class EKind : uint8 { None, Start, End };

    EKind Outcome = EKind::None;
    double IdleAfterSec = 0.0;     // valid for Start
    double IdleDurationSec = 0.0;  // valid for End
};

/**
 * Pure-logic kernel for FPlayloopAutoInstrument. Holds the rolling FPS
 * window, the idle state machine, and the rate-limiting state for the
 * auto-emitted events. Kept free of any World / Slate / FApp glue so the
 * logic is covered by plain automation specs (the driver pumps real values
 * in each tick).
 *
 * Mirrors the Unity SDK's AutoInstrumentEngine + Godot's
 * auto_instrument_engine.gd 1:1 (thresholds, cooldowns, window length, and
 * payload field semantics) so server-side math never branches by SDK.
 */
class PLAYLOOPSDK_API FPlayloopAutoInstrumentEngine
{
public:
    /** Min seconds between consecutive fps_drop events. */
    static constexpr double FpsDropCooldownSec = 5.0;
    /** Rolling-window length (seconds) for the median FPS calculation. */
    static constexpr double FpsWindowSec = 3.0;
    /** Min seconds between consecutive memory_pressure events. */
    static constexpr double MemoryCooldownSec = 30.0;
    /** Need at least this many frames in the window before trusting the median. */
    static constexpr int32 FpsMinSamples = 30;

    explicit FPlayloopAutoInstrumentEngine(double InIdleThresholdSec = 30.0, double InFpsDropThreshold = 30.0);

    double GetIdleThresholdSec() const { return IdleThresholdSec; }
    double GetFpsDropThreshold() const { return FpsDropThreshold; }

    /** Read-only diagnostic: is the engine currently in the idle state? */
    bool IsIdle() const { return bIsIdle; }

    /**
     * Decide whether to emit a memory_pressure event. Returns false when
     * called inside the cooldown window so we don't spam.
     */
    bool TryRecordMemoryEvent(double NowSec);

    /**
     * Record one frame of FPS data and report whether an fps_drop should
     * fire. Trailing-3s window; median is the middle frame-rate of the kept
     * samples. Throttled to one drop every FpsDropCooldownSec so a sustained
     * slow patch produces one event, not 180/min.
     */
    FPlayloopFpsDropDecision RecordFrame(double DeltaSec, double NowSec);

    /**
     * Run the idle state machine for one tick. Pass bInputThisFrame = true on
     * every frame where the user pressed a key, moved the mouse, or touched
     * the screen. Flips to idle after IdleThresholdSec with no input, and
     * back to active the moment input returns.
     */
    FPlayloopIdleDecision Tick(bool bInputThisFrame, double NowSec);

    /** Stamp "input just happened" without running the rest of the tick. */
    void NoteInput(double NowSec) { LastInputAtSec = NowSec; }

    /** Reset the idle clock so the next Tick treats `now` as the most recent input. */
    void ResetIdle(double NowSec);

private:
    double IdleThresholdSec = 30.0;
    double FpsDropThreshold = 30.0;

    // fps rolling window: per-frame deltas (sec) + the time each was recorded.
    TArray<double> FpsDeltas;
    TArray<double> FpsTimes;
    double LastFpsDropAtSec = -TNumericLimits<double>::Max();

    // memory cooldown
    double LastMemoryEventAtSec = -TNumericLimits<double>::Max();

    // idle state
    bool bIsIdle = false;
    double IdleEnteredAtSec = 0.0;
    double LastInputAtSec = 0.0;
};

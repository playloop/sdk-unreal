#include "PlayloopAutoInstrumentEngine.h"

FPlayloopAutoInstrumentEngine::FPlayloopAutoInstrumentEngine(double InIdleThresholdSec, double InFpsDropThreshold)
    : IdleThresholdSec(InIdleThresholdSec > 0.0 ? InIdleThresholdSec : 30.0)
    , FpsDropThreshold(InFpsDropThreshold > 0.0 ? InFpsDropThreshold : 30.0)
{
    FpsDeltas.Reserve(256);
    FpsTimes.Reserve(256);
}

bool FPlayloopAutoInstrumentEngine::TryRecordMemoryEvent(double NowSec)
{
    if (NowSec - LastMemoryEventAtSec < MemoryCooldownSec)
    {
        return false;
    }
    LastMemoryEventAtSec = NowSec;
    return true;
}

FPlayloopFpsDropDecision FPlayloopAutoInstrumentEngine::RecordFrame(double DeltaSec, double NowSec)
{
    FPlayloopFpsDropDecision Out;
    if (DeltaSec <= 0.0 || FMath::IsNaN(DeltaSec) || !FMath::IsFinite(DeltaSec))
    {
        return Out;
    }

    FpsDeltas.Add(DeltaSec);
    FpsTimes.Add(NowSec);

    // Drop samples older than the window. Both arrays move in lockstep, so a
    // single pivot search keeps them aligned.
    const double Cutoff = NowSec - FpsWindowSec;
    int32 Trim = 0;
    while (Trim < FpsTimes.Num() && FpsTimes[Trim] < Cutoff)
    {
        ++Trim;
    }
    if (Trim > 0)
    {
        FpsTimes.RemoveAt(0, Trim, EAllowShrinking::No);
        FpsDeltas.RemoveAt(0, Trim, EAllowShrinking::No);
    }

    // Need at least a second of data before we trust the median.
    if (FpsDeltas.Num() < FpsMinSamples)
    {
        return Out;
    }

    // Convert deltas to instantaneous FPS, sort a copy, pick the median.
    TArray<double> FpsCopy;
    FpsCopy.Reserve(FpsDeltas.Num());
    for (double D : FpsDeltas)
    {
        FpsCopy.Add(1.0 / D);
    }
    FpsCopy.Sort();
    const double Median = FpsCopy[FpsCopy.Num() / 2];
    const double Low = FpsCopy[0];

    if (Median >= FpsDropThreshold)
    {
        return Out;
    }
    if (NowSec - LastFpsDropAtSec < FpsDropCooldownSec)
    {
        return Out;
    }

    LastFpsDropAtSec = NowSec;
    Out.bFire = true;
    Out.MedianFps = FMath::RoundToDouble(Median * 100.0) / 100.0;
    Out.LowFps = FMath::RoundToDouble(Low * 100.0) / 100.0;
    Out.WindowSec = FpsWindowSec;
    return Out;
}

FPlayloopIdleDecision FPlayloopAutoInstrumentEngine::Tick(bool bInputThisFrame, double NowSec)
{
    FPlayloopIdleDecision Out;

    if (bInputThisFrame)
    {
        LastInputAtSec = NowSec;
        if (bIsIdle)
        {
            const double Dur = NowSec - IdleEnteredAtSec;
            bIsIdle = false;
            Out.Outcome = FPlayloopIdleDecision::EKind::End;
            Out.IdleDurationSec = FMath::RoundToDouble(Dur * 100.0) / 100.0;
            return Out;
        }
        return Out;
    }

    if (!bIsIdle && NowSec - LastInputAtSec >= IdleThresholdSec)
    {
        bIsIdle = true;
        IdleEnteredAtSec = NowSec;
        Out.Outcome = FPlayloopIdleDecision::EKind::Start;
        Out.IdleAfterSec = IdleThresholdSec;
        return Out;
    }

    return Out;
}

void FPlayloopAutoInstrumentEngine::ResetIdle(double NowSec)
{
    bIsIdle = false;
    LastInputAtSec = NowSec;
    IdleEnteredAtSec = 0.0;
}

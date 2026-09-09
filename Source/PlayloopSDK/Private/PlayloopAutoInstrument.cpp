#include "PlayloopAutoInstrument.h"
#include "PlayloopAutoInstrumentEngine.h"
#include "PlayloopSDK.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"

// Slate drives the idle + focus signals. It isn't present in a dedicated-
// server / commandlet build, so every access is guarded by
// FSlateApplication::IsInitialized().
#include "Framework/Application/SlateApplication.h"

FPlayloopAutoInstrument::FPlayloopAutoInstrument(FTrackSink InTrack)
    : Track(InTrack)
{
}

FPlayloopAutoInstrument::~FPlayloopAutoInstrument()
{
    Stop();
}

double FPlayloopAutoInstrument::NowSec()
{
    return FPlatformTime::Seconds();
}

void FPlayloopAutoInstrument::Start(const FPlayloopAutoInstrumentSettings& InSettings)
{
    if (bStarted)
    {
        return;
    }
    Settings = InSettings;
    if (!Settings.AnyEnabled())
    {
        return;
    }

    // Resolve the idle threshold default (30s desktop / 60s mobile) on the
    // game thread; PLATFORM_DESKTOP is a compile-time macro.
    double IdleThreshold = Settings.IdleThresholdSec;
    if (IdleThreshold <= 0.0)
    {
#if PLATFORM_DESKTOP
        IdleThreshold = 30.0;
#else
        IdleThreshold = 60.0;
#endif
    }
    Engine = MakeUnique<FPlayloopAutoInstrumentEngine>(IdleThreshold, Settings.FpsDropThreshold);
    Engine->ResetIdle(NowSec());

    // Focus: fire on the activation-state transition. Slate's delegate runs
    // on the game thread the instant the window gains/loses activation.
    if (Settings.bFocus && FSlateApplication::IsInitialized())
    {
        bHasFocus = FSlateApplication::Get().IsActive();
        FSlateApplication::Get().OnApplicationActivationStateChanged()
            .AddRaw(this, &FPlayloopAutoInstrument::OnApplicationActivationChanged);
    }

    // Scene: OnPostWorldInitialization fires when a new world (map) comes up.
    if (Settings.bScenes)
    {
        PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization
            .AddRaw(this, &FPlayloopAutoInstrument::OnPostWorldInit);
        LastSceneName = CurrentSceneName();
        bSceneBaselineSet = true;
    }

    // Per-frame ticker for fps + idle + memory. FTSTicker ticks once per frame
    // on the game thread, which is exactly the cadence the kernel expects.
    if (Settings.bFps || Settings.bIdle || Settings.bMemory)
    {
        TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FPlayloopAutoInstrument::TickFrame));
    }

    bStarted = true;
}

void FPlayloopAutoInstrument::Stop()
{
    if (!bStarted)
    {
        return;
    }
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
    if (PostWorldInitHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
        PostWorldInitHandle.Reset();
    }
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this);
    }
    Engine.Reset();
    bStarted = false;
    bSceneBaselineSet = false;
    bInteractionBaselineSet = false;
}

bool FPlayloopAutoInstrument::TickFrame(float DeltaTime)
{
    if (!Engine.IsValid() || !Track.IsBound())
    {
        // Returning true keeps the ticker alive; Stop() removes it.
        return true;
    }
    const double Now = NowSec();
    // FApp::GetDeltaTime is the engine's authoritative frame delta and is
    // robust to the ticker's own DeltaTime quirks (e.g. when time-dilated).
    const double Dt = FApp::GetDeltaTime();

    if (Settings.bFps)
    {
        const FPlayloopFpsDropDecision Fps = Engine->RecordFrame(Dt, Now);
        if (Fps.bFire)
        {
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetNumberField(TEXT("medianFps"), Fps.MedianFps);
            Data->SetNumberField(TEXT("lowFps"), Fps.LowFps);
            Data->SetNumberField(TEXT("windowSec"), Fps.WindowSec);
            Track.Execute(TEXT("fps_drop"), Data);
        }
    }

    if (Settings.bIdle)
    {
        const FPlayloopIdleDecision Idle = Engine->Tick(DetectInputThisFrame(), Now);
        switch (Idle.Outcome)
        {
            case FPlayloopIdleDecision::EKind::Start:
            {
                TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("idleAfterSec"), Idle.IdleAfterSec);
                Track.Execute(TEXT("idle_start"), Data);
                break;
            }
            case FPlayloopIdleDecision::EKind::End:
            {
                TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("idleDurationSec"), Idle.IdleDurationSec);
                Track.Execute(TEXT("idle_end"), Data);
                break;
            }
            default:
                break;
        }
    }

    if (Settings.bMemory)
    {
        const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
        // TotalPhysical = installed RAM; UsedPhysical = this process's resident
        // set. Fire when the process is within 90% of total physical RAM.
        const uint64 TotalBytes = Stats.TotalPhysical;
        const uint64 AllocBytes = Stats.UsedPhysical;
        if (TotalBytes > 0 && AllocBytes >= static_cast<uint64>(static_cast<double>(TotalBytes) * 0.9))
        {
            if (Engine->TryRecordMemoryEvent(Now))
            {
                TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
                Data->SetNumberField(TEXT("totalMb"),
                    FMath::RoundToDouble(static_cast<double>(TotalBytes) / (1024.0 * 1024.0) * 10.0) / 10.0);
                Data->SetNumberField(TEXT("allocatedMb"),
                    FMath::RoundToDouble(static_cast<double>(AllocBytes) / (1024.0 * 1024.0) * 10.0) / 10.0);
                Track.Execute(TEXT("memory_pressure"), Data);
            }
        }
    }

    return true; // keep ticking
}

bool FPlayloopAutoInstrument::DetectInputThisFrame()
{
    // Slate tracks the last user-interaction time (keyboard / mouse / touch /
    // gamepad). A forward jump since the last tick means input happened.
    // Best-effort: headless / server builds have no Slate, so report "no
    // input" rather than crashing the idle path.
    if (!FSlateApplication::IsInitialized())
    {
        return false;
    }
    const double Last = FSlateApplication::Get().GetLastUserInteractionTime();
    if (!bInteractionBaselineSet)
    {
        LastInteractionSeen = Last;
        bInteractionBaselineSet = true;
        return false;
    }
    if (Last > LastInteractionSeen)
    {
        LastInteractionSeen = Last;
        return true;
    }
    return false;
}

void FPlayloopAutoInstrument::OnApplicationActivationChanged(bool bIsActive)
{
    if (!Settings.bFocus || !Track.IsBound())
    {
        return;
    }
    if (bIsActive == bHasFocus)
    {
        return; // not a real transition
    }
    bHasFocus = bIsActive;

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("timestampMs"),
        static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000.0);
    Track.Execute(bIsActive ? TEXT("focus_gained") : TEXT("focus_lost"), Data);
}

void FPlayloopAutoInstrument::OnPostWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
    if (!Settings.bScenes || !Track.IsBound() || World == nullptr)
    {
        return;
    }
    // Only game/PIE worlds count as a "scene change"; editor/preview/inactive
    // worlds spin up constantly and would spam the event.
    const EWorldType::Type Type = World->WorldType;
    if (Type != EWorldType::Game && Type != EWorldType::PIE)
    {
        return;
    }
    const FString To = World->GetMapName().IsEmpty() ? CurrentSceneName() : World->GetMapName();
    const FString From = bSceneBaselineSet ? LastSceneName : TEXT("<initial>");
    if (To == LastSceneName)
    {
        return;
    }
    LastSceneName = To;
    bSceneBaselineSet = true;

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("from"), From.IsEmpty() ? TEXT("<initial>") : From);
    Data->SetStringField(TEXT("to"), To.IsEmpty() ? TEXT("<initial>") : To);
    Track.Execute(TEXT("scene_changed"), Data);
}

FString FPlayloopAutoInstrument::CurrentSceneName()
{
    if (!GEngine)
    {
        return TEXT("<initial>");
    }
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        if ((Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE) && Ctx.World())
        {
            const FString Name = Ctx.World()->GetMapName();
            return Name.IsEmpty() ? TEXT("<initial>") : Name;
        }
    }
    return TEXT("<initial>");
}

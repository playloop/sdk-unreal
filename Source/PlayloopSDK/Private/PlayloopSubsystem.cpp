#include "PlayloopSubsystem.h"
#include "PlayloopClient.h"
#include "PlayloopIdentityStore.h"
#include "PlayloopSDK.h"
#include "PlayloopSettings.h"

void UPlayloopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UPlayloopSettings* Settings = GetDefault<UPlayloopSettings>();
    if (!Settings || !Settings->bAutoStart)
    {
        UE_LOG(LogPlayloop, Verbose, TEXT("Auto-start disabled. Skipping subsystem boot."));
        return;
    }
    if (Settings->ApiKey.IsEmpty())
    {
        UE_LOG(LogPlayloop, Warning,
            TEXT("Playloop auto-start is enabled but no API key is set. The client will not start and GetClient() will return null. Set ApiKey under Project Settings → Plugins → Playloop (persisted to Config/DefaultPlayloop.ini under [/Script/PlayloopSDK.PlayloopSettings]), not DefaultEngine.ini."));
        return;
    }
    // The per-game ingest key alone identifies the game for every feature, so
    // there's nothing else to require before auto-start.

    Client = NewObject<UPlayloopClient>(this);

    FPlayloopConfig Config;
    Config.ApiKey = Settings->ApiKey;
    Config.Environment = Settings->ResolvedEnvironment();
    Config.BuildVersion = Settings->BuildVersionOverride;
    Config.ApiBaseUrl = Settings->ApiBaseUrl;
    Config.FlushIntervalMs = Settings->FlushIntervalMs;
    Config.HeartbeatIntervalMs = Settings->HeartbeatIntervalMs;
    Config.bEnableCrashHandler = Settings->bEnableCrashHandler;
    Config.CrashRecentEventsCount = Settings->CrashRecentEventsCount;
    Config.bPrefetchExperiments = Settings->bPrefetchExperiments;
    Config.bSendInEditor = Settings->bSendInEditor;
    Config.bAutoInstrumentScenes = Settings->bAutoInstrumentScenes;
    Config.bAutoInstrumentFocus = Settings->bAutoInstrumentFocus;
    Config.bAutoInstrumentIdle = Settings->bAutoInstrumentIdle;
    Config.bAutoInstrumentFps = Settings->bAutoInstrumentFps;
    Config.bAutoInstrumentMemory = Settings->bAutoInstrumentMemory;
    Config.IdleThresholdSec = Settings->IdleThresholdSec;
    Config.FpsDropThreshold = Settings->FpsDropThreshold;

    Client->Configure(Config);
    Client->Start();
}

void UPlayloopSubsystem::Deinitialize()
{
    if (Client)
    {
        Client->EndSession();
        Client = nullptr;
    }
    Super::Deinitialize();
}

void UPlayloopSubsystem::SetLinkedId(const FString& LinkedId)
{
    FPlayloopIdentityStore::SetLinkedId(LinkedId);
}

void UPlayloopSubsystem::SetConsent(const FString& Level)
{
    FPlayloopIdentityStore::SetConsent(Level);
}

FString UPlayloopSubsystem::GetConsent() const
{
    return FPlayloopIdentityStore::GetConsent();
}

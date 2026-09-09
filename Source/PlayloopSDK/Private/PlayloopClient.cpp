#include "PlayloopClient.h"
#include "PlayloopAutoInstrument.h"
#include "PlayloopBugReport.h"
#include "PlayloopCrashHandler.h"
#include "PlayloopDeviceId.h"
#include "PlayloopDiscord.h"
#include "PlayloopEventConfigStore.h"
#include "PlayloopEventQueue.h"
#include "PlayloopExperiments.h"
#include "PlayloopFeedbackWidget.h"
#include "PlayloopGameResolver.h"
#include "PlayloopHttpClient.h"
#include "PlayloopIdentityStore.h"
#include "PlayloopSDK.h"
#include "PlayloopSettings.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
// UE 5.8 IWYU: UGameViewportClient is accessed as a complete type (member
// access) and is no longer transitively included via Engine.h.
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

namespace
{
    constexpr const TCHAR* kSdkVersion = TEXT("playloop-unreal@0.5.0");

    int64 NowMs()
    {
        return static_cast<int64>(FDateTime::UtcNow().ToUnixTimestamp() * 1000);
    }

    UWorld* ResolveWorld()
    {
        if (!GEngine) return nullptr;
        if (GEngine->GameViewport && GEngine->GameViewport->GetWorld())
        {
            return GEngine->GameViewport->GetWorld();
        }
        if (FWorldContext* Ctx = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport))
        {
            return Ctx->World();
        }
        return nullptr;
    }

    FString SimplePlatformName()
    {
#if PLATFORM_WINDOWS
        return TEXT("Unreal/Windows");
#elif PLATFORM_MAC
        return TEXT("Unreal/Mac");
#elif PLATFORM_LINUX
        return TEXT("Unreal/Linux");
#elif PLATFORM_ANDROID
        return TEXT("Unreal/Android");
#elif PLATFORM_IOS
        return TEXT("Unreal/iOS");
#else
        return TEXT("Unreal/Unknown");
#endif
    }

    // True when this process is the Unreal editor (PIE / -game from editor) or
    // an editor commandlet. Drives both the env auto-derive and the
    // suppress-in-editor gate.
    bool IsEditorRuntime()
    {
#if WITH_EDITOR
        return GIsEditor;
#else
        return false;
#endif
    }

    // True for a non-shipping build: the editor, a Debug build, or a
    // Development build. A packaged Shipping player build is the only thing
    // this returns false for. Matches Unity's "development build" notion.
    bool IsDevelopmentBuild()
    {
#if WITH_EDITOR
        if (IsEditorRuntime()) return true;
#endif
#if UE_BUILD_SHIPPING
        return false;
#else
        // UE_BUILD_DEBUG / UE_BUILD_DEVELOPMENT / UE_BUILD_TEST all count as
        // "not a shipped player build."
        return true;
#endif
    }

    // The UE engine version, trimmed to major.minor.patch (e.g. "5.8.0") so
    // the fingerprint stays clean. The full ToString() trails changelist +
    // branch metadata we don't need on the wire.
    FString EngineVersionString()
    {
        return FEngineVersion::Current().ToString(EVersionComponent::Patch);
    }

    // Auto-derive an environment slug from the build when the consumer left
    // Environment at its default ("dev") or empty: the editor / a Development /
    // Debug build stays "dev", while a packaged Shipping player build reports
    // "production". An explicit non-default value (Production / Demo / Custom /
    // any other slug) always wins and is returned verbatim.
    FString ResolveEnvironment(const FString& Configured)
    {
        const FString Trimmed = Configured.TrimStartAndEnd();
        const bool bIsDefault = Trimmed.IsEmpty() || Trimmed.Equals(TEXT("dev"), ESearchCase::IgnoreCase);
        const FString Resolved = bIsDefault ? TEXT("dev") : Trimmed;
        if (Resolved.Equals(TEXT("dev"), ESearchCase::IgnoreCase) && !IsDevelopmentBuild())
        {
            return TEXT("production");
        }
        return Resolved;
    }

    UPlayloopClient* GCurrent = nullptr;
}

UPlayloopClient::UPlayloopClient()
    : Queue(MakeShared<FPlayloopEventQueue>())
    , Http(MakeShared<FPlayloopHttpClient>())
{
}

UPlayloopClient::~UPlayloopClient()
{
    StopAutoInstrument();
    StopTimers();
    if (GCurrent == this)
    {
        GCurrent = nullptr;
    }
}

void UPlayloopClient::Configure(const FPlayloopConfig& InConfig)
{
    Config = InConfig;
    if (Config.ApiBaseUrl.IsEmpty())
    {
        Config.ApiBaseUrl = TEXT("https://playloop.gg");
    }
    else
    {
        // Normalize the base URL. UE's .ini parser truncates an unquoted
        // "http://host:port" at the "//", so the value can arrive as a bare
        // scheme ("http:"); left unchecked we'd build "http:///api/..." and
        // silently send every request to the wrong host. Trim, drop trailing
        // slashes, and warn (then fall back) on a bare/malformed scheme so a
        // config slip surfaces as a log line instead of a telemetry blackout.
        Config.ApiBaseUrl = Config.ApiBaseUrl.TrimStartAndEnd();
        while (Config.ApiBaseUrl.RemoveFromEnd(TEXT("/"))) {}
        if (Config.ApiBaseUrl.IsEmpty()
            || Config.ApiBaseUrl.Equals(TEXT("http:"), ESearchCase::IgnoreCase)
            || Config.ApiBaseUrl.Equals(TEXT("https:"), ESearchCase::IgnoreCase))
        {
            UE_LOG(LogPlayloop, Warning,
                TEXT("Playloop ApiBaseUrl '%s' is malformed (bare scheme). If set in DefaultPlayloop.ini, quote it: ApiBaseUrl=\"https://playloop.gg\". Falling back to https://playloop.gg."),
                *InConfig.ApiBaseUrl);
            Config.ApiBaseUrl = TEXT("https://playloop.gg");
        }
    }
    if (Config.MaxHttpRetries <= 0)
    {
        Config.MaxHttpRetries = 3;
    }
    // Heartbeat floor: 0 stays "disabled", but any positive cadence below the
    // 30s minimum is clamped up to 30000ms so a misconfigured interval can't
    // hammer the server. Default is 60000ms (one minute). Mirrors the
    // MIN_HEARTBEAT_SEC clamp in the Unity/Godot/Python/TS SDKs.
    if (Config.HeartbeatIntervalMs > 0 && Config.HeartbeatIntervalMs < 30000)
    {
        UE_LOG(LogPlayloop, Warning,
            TEXT("Playloop HeartbeatIntervalMs %d is below the 30000ms (30s) minimum; clamping to 30000. Set 0 to disable heartbeats."),
            Config.HeartbeatIntervalMs);
        Config.HeartbeatIntervalMs = 30000;
    }
    // Resolve the environment once. When left at the default ("dev") or
    // empty, auto-derive it from the build (the editor / a Development build
    // stays "dev"; a packaged Shipping build → "production"). An explicit slug
    // wins. The header + the session-metadata stamp + every sub-API all read
    // this single resolved value.
    Config.Environment = ResolveEnvironment(Config.Environment);
    // Bind the experiments API to the resolved config + shared HTTP client.
    // A fresh Configure() resets the session-scoped assignment cache.
    if (!ExperimentsObj)
    {
        ExperimentsObj = NewObject<UPlayloopExperiments>(this);
    }
    ExperimentsObj->Initialize(Config, Http);

    // Bind the Discord ingest API to the resolved config + shared HTTP client.
    if (!DiscordObj)
    {
        DiscordObj = NewObject<UPlayloopDiscord>(this);
    }
    DiscordObj->Initialize(Config, Http);

    // Resolve the game identity (gameId + slug + name) from the ingest key,
    // lazily and cached. Telemetry never awaits it; event-config + Tester Keys
    // do. Rebuilt on every Configure() so a fresh config (e.g. a different key)
    // drops the prior cache.
    Resolver = MakeShared<FPlayloopGameResolver>(
        Http, Config.ApiKey, Config.ApiBaseUrl, Config.Environment, Config.MaxHttpRetries);

    // Bind the per-event config store. Rebuilt on every Configure(). The store
    // resolves the slug from the ingest key; it stays inert (Lookup returns
    // nothing) when the key can't be resolved.
    EventConfig = MakeShared<FPlayloopEventConfigStore>(
        Http, Resolver, Config.ApiKey, Config.ApiBaseUrl,
        Config.Environment, Config.MaxHttpRetries);
}

UPlayloopExperiments* UPlayloopClient::Experiments()
{
    if (!ExperimentsObj)
    {
        ExperimentsObj = NewObject<UPlayloopExperiments>(this);
        ExperimentsObj->Initialize(Config, Http);
    }
    return ExperimentsObj;
}

UPlayloopDiscord* UPlayloopClient::Discord()
{
    if (!DiscordObj)
    {
        DiscordObj = NewObject<UPlayloopDiscord>(this);
        DiscordObj->Initialize(Config, Http);
    }
    return DiscordObj;
}

void UPlayloopClient::Start()
{
    if (bStarted)
    {
        return;
    }
    if (Config.ApiKey.IsEmpty())
    {
        UE_LOG(LogPlayloop, Warning, TEXT("Start() called without an API key. Configure() it first."));
        return;
    }
    // The per-game ingest key alone identifies the game: telemetry,
    // experiments, event-config, and Tester Keys all resolve from it. No slug
    // or id is required to start.

    bStarted = true;
    CurrentSessionId.Reset();

    // Suppress-in-editor gate. In the editor or a non-shipping (Debug /
    // Development) build, telemetry sends are suppressed by default so local
    // playtesting doesn't pollute real session data. Opt in with bSendInEditor
    // (pair it with Environment = "dev" to keep the traffic filterable). A
    // packaged Shipping build always sends. Logged once at Start().
    bSendSuppressed = IsDevelopmentBuild() && !Config.bSendInEditor;
    if (bSendSuppressed)
    {
        UE_LOG(LogPlayloop, Warning,
            TEXT("Playloop telemetry is SUPPRESSED in the editor / non-shipping build so it doesn't pollute real data. To send from the editor or a dev build, enable bSendInEditor (Project Settings → Plugins → Playloop → Advanced, or FPlayloopConfig::bSendInEditor) and set Environment = \"dev\" so it's easy to filter out. Shipping builds always send."));
    }

    if (Config.bEnableCrashHandler)
    {
        // Install the crash trap now. gated on a started, crash-handling
        // client. rather than unconditionally at module load, so a game that
        // never configures Playloop leaves no orphaned pending-crashes file.
        // Idempotent across Start()/Configure() churn.
        FPlayloopCrashHandler::Get().Initialize();
        FPlayloopCrashContext CrashCtx;
        CrashCtx.SdkVersion = kSdkVersion;
        CrashCtx.BuildVersion = Config.BuildVersion;
        CrashCtx.Platform = SimplePlatformName();
        FPlayloopCrashHandler::Get().SetCurrentContext(CrashCtx);
        DrainCrashes();
    }
    EmitSessionStart();
    // Warm the game-identity resolve so the first event-config / Tester Keys
    // call doesn't pay the round-trip. Never blocks Start().
    if (Resolver.IsValid())
    {
        Resolver->Prefetch();
    }
    // Fire-and-forget pull of the per-event config (sdkIgnore / linkToSummary
    // + the per-game settings block the feedback widget reads for branding).
    // The store resolves the slug from the ingest key and no-ops if that
    // fails; never blocks Start().
    if (EventConfig.IsValid())
    {
        EventConfig->Refresh();
    }
    // Eager experiment prefetch (opt-in). Warms the assignment cache so the
    // first VariantAsync() resolves without a round-trip, for games that
    // gate the opening moments on a variant.
    if (Config.bPrefetchExperiments && ExperimentsObj)
    {
        ExperimentsObj->RefreshAsync();
    }
    // Warm the feedback widget's brand mark off the game thread so the first
    // form open (often mid-playtest) doesn't pay a synchronous disk read.
    UPlayloopFeedbackWidget::PreloadBranding();
    StartTimers();
    StartAutoInstrument();
    SetCurrent(this);

    UE_LOG(LogPlayloop, Log, TEXT("Playloop client started (env=%s)."), *Config.Environment);
}

void UPlayloopClient::Stop()
{
    if (!bStarted)
    {
        return;
    }
    StopAutoInstrument();
    StopTimers();
    Flush();
    bStarted = false;
    CurrentSessionId.Reset();
}

void UPlayloopClient::Flush()
{
    if (Queue->PendingCount() == 0)
    {
        return;
    }
    if (bSendSuppressed)
    {
        // Editor / dev-build suppression: drop the buffered events instead of
        // sending or growing the queue unbounded. The one-time warning fired
        // at Start().
        Queue->DrainPending();
        return;
    }
    TArray<FPlayloopEvent> Events = Queue->DrainPending();
    TSharedPtr<FJsonObject> SessionMeta;
    if (CurrentSessionId.IsEmpty())
    {
        SessionMeta = MakeShared<FJsonObject>();
        SessionMeta->SetStringField(TEXT("sdkVersion"), kSdkVersion);
        SessionMeta->SetStringField(TEXT("buildVersion"), Config.BuildVersion);
        SessionMeta->SetStringField(TEXT("platform"), SimplePlatformName());
        // Engine fingerprint + editor flag: cross-SDK metadata.
        SessionMeta->SetStringField(TEXT("engine"), TEXT("unreal"));
        SessionMeta->SetStringField(TEXT("engineVersion"), EngineVersionString());
        SessionMeta->SetBoolField(TEXT("isEditor"), IsEditorRuntime());
    }
    // Cross-game identity. Only attach LinkedId / ConsentLevel
    // on the session-create flush (CurrentSessionId still empty pre-
    // first-response). Server-side resolver stitches identity on the
    // first event ingest of a launch.
    FString PendingLinkedId;
    FString PendingConsentLevel;
    TMap<FString, FString> ExperimentTags;
    if (CurrentSessionId.IsEmpty())
    {
        PendingLinkedId = FPlayloopIdentityStore::GetLinkedId();
        PendingConsentLevel = FPlayloopIdentityStore::GetConsent();
        // Tag the variant(s) the player saw onto the session-create flush.
        // The server re-validates each tag before persisting.
        if (ExperimentsObj)
        {
            ExperimentTags = ExperimentsObj->Snapshot();
        }
    }
    const FString Envelope = FPlayloopEventQueue::BuildEnvelopeJson(
        Events, CurrentSessionId, GetDeviceId(), Config.Environment,
        SessionMeta, nullptr, false,
        PendingLinkedId, PendingConsentLevel, ExperimentTags);
    TWeakObjectPtr<UPlayloopClient> WeakSelf(this);
    Http->PostJson(ResolveTelemetryUrl(), Config.ApiKey, Envelope, Config.Environment, Config.MaxHttpRetries,
        FOnPlayloopHttpDone::CreateLambda([WeakSelf, Events](FPlayloopHttpResult Result)
        {
            UPlayloopClient* Self = WeakSelf.Get();
            if (!Self) return;
            if (!Result.bOk)
            {
                Self->Queue->RestoreFront(Events);
                UE_LOG(LogPlayloop, Warning, TEXT("Playloop flush failed (%s); will retry on next interval."), *Result.ErrorReason);
                return;
            }
            TSharedPtr<FJsonObject> Body;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
            if (FJsonSerializer::Deserialize(Reader, Body) && Body.IsValid())
            {
                FString AssignedId;
                if (Body->TryGetStringField(TEXT("sessionId"), AssignedId) && !AssignedId.IsEmpty())
                {
                    Self->CurrentSessionId = AssignedId;
                    if (Self->Config.bEnableCrashHandler)
                    {
                        FPlayloopCrashContext Ctx;
                        Ctx.CrashedSessionId = AssignedId;
                        Ctx.SdkVersion = kSdkVersion;
                        Ctx.BuildVersion = Self->Config.BuildVersion;
                        Ctx.Platform = SimplePlatformName();
                        FPlayloopCrashHandler::Get().SetCurrentContext(Ctx);
                    }
                }
            }
        }));
}

void UPlayloopClient::EndSession()
{
    if (!bStarted)
    {
        return;
    }
    StopAutoInstrument();
    EmitSessionEnd();
    Flush();
    StopTimers();
    bStarted = false;
}

void UPlayloopClient::Track(const FString& EventName, const FString& JsonProps)
{
    TSharedPtr<FJsonObject> Props;
    if (!JsonProps.IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonProps);
        FJsonSerializer::Deserialize(Reader, Props);
    }
    if (!Props.IsValid())
    {
        Props = MakeShared<FJsonObject>();
    }
    TrackJson(EventName, Props);
}

void UPlayloopClient::TrackJson(const FString& EventName, TSharedPtr<FJsonObject> Props)
{
    // Per-event server config. When the store hasn't fetched yet (or has no row
    // for this event) Lookup returns false and the event flows through normally.
    if (EventConfig.IsValid())
    {
        FPlayloopEventConfigEntry Cfg;
        if (EventConfig->Lookup(EventName, Cfg))
        {
            if (Cfg.bSdkIgnore)
            {
                // Drop the event before it enters the buffer.
                return;
            }
            if (Cfg.bLinkToSummary && Props.IsValid() && Props->Values.Num() > 0)
            {
                MergeIntoState(Props);
            }
        }
    }

    FPlayloopEvent Event;
    Event.Id = FPlayloopEventQueue::GenerateShortId();
    Event.Name = EventName;
    Event.TimestampMs = NowMs();
    Event.Data = Props.IsValid() ? Props : MakeShared<FJsonObject>();
    Queue->Enqueue(Event);
}

void UPlayloopClient::SetStateBool(const FString& Key, bool Value)
{
    FScopeLock Guard(&StateMutex);
    State.Add(Key, MakeShared<FJsonValueBoolean>(Value));
}
void UPlayloopClient::SetStateNumber(const FString& Key, double Value)
{
    FScopeLock Guard(&StateMutex);
    State.Add(Key, MakeShared<FJsonValueNumber>(Value));
}
void UPlayloopClient::SetStateString(const FString& Key, const FString& Value)
{
    FScopeLock Guard(&StateMutex);
    State.Add(Key, MakeShared<FJsonValueString>(Value));
}
void UPlayloopClient::IncrementState(const FString& Key, double Delta)
{
    FScopeLock Guard(&StateMutex);
    double Current = 0.0;
    if (TSharedPtr<FJsonValue>* Existing = State.Find(Key))
    {
        if (Existing->IsValid() && (*Existing)->Type == EJson::Number)
        {
            Current = (*Existing)->AsNumber();
        }
    }
    State.Add(Key, MakeShared<FJsonValueNumber>(Current + Delta));
}
void UPlayloopClient::ClearState()
{
    FScopeLock Guard(&StateMutex);
    State.Reset();
}

// The Tester Keys endpoints (/api/telemetry/tester + /claim) still take a
// gameId on the wire, but the ingest key is the single source of truth. We
// resolve the gameId from the key (FPlayloopGameResolver), never from config.
// Each method resolves first, then issues its request; a resolve failure
// reports a clear error so the caller sees why the link couldn't happen.

void UPlayloopClient::LinkTester(const FString& ClaimToken, const FOnPlayloopTesterLinked& OnDone)
{
    if (!Resolver.IsValid())
    {
        FPlayloopLinkResult Failure;
        Failure.ErrorCode = TEXT("resolve_failed");
        Failure.ErrorMessage = TEXT("Playloop client is not configured.");
        if (OnDone.IsBound()) OnDone.Execute(Failure);
        return;
    }
    TWeakObjectPtr<UPlayloopClient> WeakSelf(this);
    Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
        [WeakSelf, ClaimToken, OnDone](bool bOk, const FPlayloopResolvedGame& Game)
        {
            UPlayloopClient* Self = WeakSelf.Get();
            if (!Self) return;
            if (!bOk || Game.GameId.IsEmpty())
            {
                FPlayloopLinkResult Failure;
                Failure.ErrorCode = TEXT("resolve_failed");
                Failure.ErrorMessage = TEXT("Could not resolve the game from your ingest key (check the key and your connection).");
                if (OnDone.IsBound()) OnDone.Execute(Failure);
                return;
            }
            TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
            Body->SetStringField(TEXT("claimToken"), ClaimToken);
            Body->SetStringField(TEXT("gameId"), Game.GameId);
            Body->SetStringField(TEXT("deviceId"), Self->GetDeviceId());
            FString Out;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
            FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
            Self->Http->PostJson(Self->ResolveClaimUrl(), Self->Config.ApiKey, Out, Self->Config.Environment, Self->Config.MaxHttpRetries,
                FOnPlayloopHttpDone::CreateLambda([OnDone](FPlayloopHttpResult Result)
                {
                    FPlayloopLinkResult Parsed;
                    TSharedPtr<FJsonObject> Json;
                    if (!Result.Body.IsEmpty())
                    {
                        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
                        FJsonSerializer::Deserialize(Reader, Json);
                    }
                    if (Result.bOk && Json.IsValid() && Json->GetBoolField(TEXT("ok")))
                    {
                        Parsed.bOk = true;
                        Json->TryGetBoolField(TEXT("alreadyLinked"), Parsed.bAlreadyLinked);
                        Json->TryGetStringField(TEXT("handle"), Parsed.Handle);
                        double ClaimedAt = 0.0;
                        if (Json->TryGetNumberField(TEXT("claimedAt"), ClaimedAt))
                        {
                            Parsed.ClaimedAtMs = static_cast<int64>(ClaimedAt);
                        }
                    }
                    else
                    {
                        Parsed.bOk = false;
                        if (Json.IsValid())
                        {
                            Json->TryGetStringField(TEXT("reason"), Parsed.ErrorCode);
                            Json->TryGetStringField(TEXT("message"), Parsed.ErrorMessage);
                        }
                        if (Parsed.ErrorCode.IsEmpty()) Parsed.ErrorCode = TEXT("http_error");
                        if (Parsed.ErrorMessage.IsEmpty()) Parsed.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Result.Status);
                    }
                    UE_LOG(LogPlayloop, Log, TEXT("LinkTester %s (status=%d)."), Parsed.bOk ? TEXT("ok") : TEXT("failed"), Result.Status);
                    if (OnDone.IsBound()) OnDone.Execute(Parsed);
                }));
        }));
}

void UPlayloopClient::GetCurrentTester(const FOnPlayloopTesterStatus& OnDone)
{
    if (!Resolver.IsValid())
    {
        if (OnDone.IsBound()) OnDone.Execute(FPlayloopTesterStatus());
        return;
    }
    TWeakObjectPtr<UPlayloopClient> WeakSelf(this);
    Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
        [WeakSelf, OnDone](bool bOk, const FPlayloopResolvedGame& Game)
        {
            UPlayloopClient* Self = WeakSelf.Get();
            if (!Self) return;
            if (!bOk || Game.GameId.IsEmpty())
            {
                // Can't resolve the game: report "not linked" (the safe default).
                if (OnDone.IsBound()) OnDone.Execute(FPlayloopTesterStatus());
                return;
            }
            const FString Url = Self->ResolveTesterStatusUrl(Game.GameId, Self->GetDeviceId());
            Self->Http->GetJson(Url, Self->Config.ApiKey, Self->Config.Environment, Self->Config.MaxHttpRetries,
                FOnPlayloopHttpDone::CreateLambda([OnDone](FPlayloopHttpResult Result)
                {
                    FPlayloopTesterStatus Parsed;
                    TSharedPtr<FJsonObject> Json;
                    if (Result.bOk && !Result.Body.IsEmpty())
                    {
                        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
                        if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
                        {
                            Json->TryGetBoolField(TEXT("linked"), Parsed.bLinked);
                            if (Parsed.bLinked)
                            {
                                Json->TryGetStringField(TEXT("handle"), Parsed.Handle);
                                double ClaimedAt = 0.0;
                                if (Json->TryGetNumberField(TEXT("claimedAt"), ClaimedAt))
                                {
                                    Parsed.ClaimedAtMs = static_cast<int64>(ClaimedAt);
                                }
                            }
                        }
                    }
                    if (OnDone.IsBound()) OnDone.Execute(Parsed);
                }));
        }));
}

void UPlayloopClient::UnlinkTester(const FString& ClaimToken, const FOnPlayloopTesterUnlinked& OnDone)
{
    if (!Resolver.IsValid())
    {
        FPlayloopUnlinkResult Failure;
        Failure.ErrorCode = TEXT("resolve_failed");
        Failure.ErrorMessage = TEXT("Playloop client is not configured.");
        if (OnDone.IsBound()) OnDone.Execute(Failure);
        return;
    }
    TWeakObjectPtr<UPlayloopClient> WeakSelf(this);
    Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
        [WeakSelf, ClaimToken, OnDone](bool bOk, const FPlayloopResolvedGame& Game)
        {
            UPlayloopClient* Self = WeakSelf.Get();
            if (!Self) return;
            if (!bOk || Game.GameId.IsEmpty())
            {
                FPlayloopUnlinkResult Failure;
                Failure.ErrorCode = TEXT("resolve_failed");
                Failure.ErrorMessage = TEXT("Could not resolve the game from your ingest key (check the key and your connection).");
                if (OnDone.IsBound()) OnDone.Execute(Failure);
                return;
            }
            TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
            Body->SetStringField(TEXT("claimToken"), ClaimToken);
            Body->SetStringField(TEXT("gameId"), Game.GameId);
            Body->SetStringField(TEXT("deviceId"), Self->GetDeviceId());
            FString Out;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
            FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
            Self->Http->DeleteJson(Self->ResolveClaimUrl(), Self->Config.ApiKey, Out, Self->Config.Environment, Self->Config.MaxHttpRetries,
                FOnPlayloopHttpDone::CreateLambda([OnDone](FPlayloopHttpResult Result)
                {
                    FPlayloopUnlinkResult Parsed;
                    TSharedPtr<FJsonObject> Json;
                    if (!Result.Body.IsEmpty())
                    {
                        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
                        FJsonSerializer::Deserialize(Reader, Json);
                    }
                    if (Result.bOk && Json.IsValid())
                    {
                        Parsed.bOk = true;
                        Json->TryGetBoolField(TEXT("unlinked"), Parsed.bUnlinked);
                    }
                    else
                    {
                        Parsed.bOk = false;
                        if (Json.IsValid())
                        {
                            Json->TryGetStringField(TEXT("reason"), Parsed.ErrorCode);
                            Json->TryGetStringField(TEXT("message"), Parsed.ErrorMessage);
                        }
                        if (Parsed.ErrorCode.IsEmpty()) Parsed.ErrorCode = TEXT("http_error");
                        if (Parsed.ErrorMessage.IsEmpty()) Parsed.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Result.Status);
                    }
                    if (OnDone.IsBound()) OnDone.Execute(Parsed);
                }));
        }));
}

void UPlayloopClient::ReportCrash(const FString& Message, const FString& StackTrace)
{
    if (!Config.bEnableCrashHandler)
    {
        return;
    }
    FPlayloopCrashHandler::Get().ReportManagedCrash(Message, StackTrace);
}

UPlayloopClient* UPlayloopClient::GetCurrent()
{
    return GCurrent;
}

void UPlayloopClient::SetCurrent(UPlayloopClient* Client)
{
    GCurrent = Client;
}

FString UPlayloopClient::GetDeviceId() const
{
    return FPlayloopDeviceId::Resolve();
}

int32 UPlayloopClient::GetPendingEventCount() const
{
    return Queue ? Queue->PendingCount() : 0;
}

void UPlayloopClient::SetHttpClientForTesting(TSharedPtr<FPlayloopHttpClient> InHttp)
{
    Http = InHttp;
}

void UPlayloopClient::EmitSessionStart()
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("sdkVersion"), kSdkVersion);
    Data->SetStringField(TEXT("buildVersion"), Config.BuildVersion);
    Data->SetStringField(TEXT("platform"), SimplePlatformName());
    Data->SetStringField(TEXT("environment"), Config.Environment);
    Data->SetStringField(TEXT("engine"), TEXT("unreal"));
    Data->SetStringField(TEXT("engineVersion"), EngineVersionString());
    Data->SetBoolField(TEXT("isEditor"), IsEditorRuntime());
    Data->SetStringField(TEXT("deviceId"), GetDeviceId());
    TrackJson(TEXT("session_start"), Data);
}

void UPlayloopClient::EmitSessionEnd()
{
    TSharedPtr<FJsonObject> Snap = SnapshotState();
    TrackJson(TEXT("session_summary"), Snap);
}

void UPlayloopClient::EmitHeartbeat()
{
    TSharedPtr<FJsonObject> Snap = SnapshotState();
    TrackJson(TEXT("session_heartbeat"), Snap);
}

TSharedPtr<FJsonObject> UPlayloopClient::SnapshotState() const
{
    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    FScopeLock Guard(&StateMutex);
    for (const auto& Pair : State)
    {
        if (!Pair.Value.IsValid()) continue;
        Out->SetField(Pair.Key, Pair.Value);
    }
    return Out;
}

void UPlayloopClient::MergeIntoState(const TSharedPtr<FJsonObject>& Data)
{
    if (!Data.IsValid())
    {
        return;
    }
    FScopeLock Guard(&StateMutex);
    for (const auto& Pair : Data->Values)
    {
        if (Pair.Value.IsValid())
        {
            State.Add(FString(Pair.Key), Pair.Value);
        }
    }
}

void UPlayloopClient::StartAutoInstrument()
{
    FPlayloopAutoInstrumentSettings AiSettings;
    AiSettings.bScenes = Config.bAutoInstrumentScenes;
    AiSettings.bFocus = Config.bAutoInstrumentFocus;
    AiSettings.bIdle = Config.bAutoInstrumentIdle;
    AiSettings.bFps = Config.bAutoInstrumentFps;
    AiSettings.bMemory = Config.bAutoInstrumentMemory;
    AiSettings.IdleThresholdSec = Config.IdleThresholdSec;
    AiSettings.FpsDropThreshold = Config.FpsDropThreshold > 0.f ? Config.FpsDropThreshold : 30.0;

    if (!AiSettings.AnyEnabled() || bTimersDisabled)
    {
        // bTimersDisabled is the test seam: specs that don't want a live
        // FTSTicker leave auto-instrument off too.
        return;
    }

    if (!AutoInstrument.IsValid())
    {
        // The sink routes auto-events back through TrackJson, so they respect
        // sdkIgnore + linkToSummary + the editor-suppression gate. Weak self so
        // a fired event after GC safely no-ops.
        TWeakObjectPtr<UPlayloopClient> WeakSelf(this);
        FPlayloopAutoInstrument::FTrackSink Sink =
            FPlayloopAutoInstrument::FTrackSink::CreateLambda(
                [WeakSelf](const FString& EventName, TSharedPtr<FJsonObject> Data)
                {
                    if (UPlayloopClient* Self = WeakSelf.Get())
                    {
                        Self->TrackJson(EventName, Data);
                    }
                });
        AutoInstrument = MakeShared<FPlayloopAutoInstrument>(Sink);
    }
    AutoInstrument->Start(AiSettings);
}

void UPlayloopClient::StopAutoInstrument()
{
    if (AutoInstrument.IsValid())
    {
        AutoInstrument->Stop();
    }
}

FPlayloopGameConfigBlock UPlayloopClient::GetGameSettings() const
{
    return EventConfig.IsValid() ? EventConfig->GameSettings() : FPlayloopGameConfigBlock();
}

bool UPlayloopClient::IsEventConfigLoaded() const
{
    return EventConfig.IsValid() && EventConfig->IsLoaded();
}

void UPlayloopClient::RefreshEventConfig()
{
    if (EventConfig.IsValid())
    {
        EventConfig->Refresh();
    }
}

void UPlayloopClient::DrainCrashes()
{
    TArray<TSharedPtr<FJsonObject>> Crashes = FPlayloopCrashHandler::Get().DrainPersisted();
    for (const TSharedPtr<FJsonObject>& Crash : Crashes)
    {
        TrackJson(TEXT("crash_reported"), Crash);
    }
}

void UPlayloopClient::StartTimers()
{
    if (bTimersDisabled)
    {
        return;
    }
    UWorld* World = ResolveWorld();
    if (!World)
    {
        UE_LOG(LogPlayloop, Warning, TEXT("No world available. Auto-flush + heartbeat disabled. Call Flush() manually."));
        return;
    }
    if (Config.FlushIntervalMs > 0)
    {
        const float Sec = Config.FlushIntervalMs / 1000.0f;
        World->GetTimerManager().SetTimer(FlushTimerHandle, FTimerDelegate::CreateUObject(this, &UPlayloopClient::OnFlushTimer), Sec, true);
    }
    if (Config.HeartbeatIntervalMs > 0)
    {
        const float Sec = Config.HeartbeatIntervalMs / 1000.0f;
        World->GetTimerManager().SetTimer(HeartbeatTimerHandle, FTimerDelegate::CreateUObject(this, &UPlayloopClient::OnHeartbeatTimer), Sec, true);
    }
}

void UPlayloopClient::StopTimers()
{
    UWorld* World = ResolveWorld();
    if (!World) return;
    if (FlushTimerHandle.IsValid()) World->GetTimerManager().ClearTimer(FlushTimerHandle);
    if (HeartbeatTimerHandle.IsValid()) World->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
}

void UPlayloopClient::OnFlushTimer() { Flush(); }
void UPlayloopClient::OnHeartbeatTimer() { EmitHeartbeat(); }

FString UPlayloopClient::ResolveTelemetryUrl() const
{
    return FString::Printf(TEXT("%s/api/telemetry"), *Config.ApiBaseUrl);
}

FString UPlayloopClient::ResolveClaimUrl() const
{
    return FString::Printf(TEXT("%s/api/telemetry/claim"), *Config.ApiBaseUrl);
}

FString UPlayloopClient::ResolveTesterStatusUrl(const FString& GameId, const FString& DeviceId) const
{
    return FString::Printf(TEXT("%s/api/telemetry/tester?gameId=%s&deviceId=%s"),
        *Config.ApiBaseUrl,
        *FGenericPlatformHttp::UrlEncode(GameId),
        *FGenericPlatformHttp::UrlEncode(DeviceId));
}

namespace
{
    const TCHAR* FieldKindToWire(EPlayloopFeedbackFieldKind Kind)
    {
        switch (Kind)
        {
            case EPlayloopFeedbackFieldKind::Rating1To5: return TEXT("rating-1-5");
            case EPlayloopFeedbackFieldKind::ShortText:  return TEXT("short-text");
            case EPlayloopFeedbackFieldKind::LongText:   return TEXT("long-text");
            case EPlayloopFeedbackFieldKind::YesNo:      return TEXT("yes-no");
        }
        return TEXT("short-text");
    }
}

void UPlayloopClient::SubmitFeedback(
    const FString& FormId,
    const TArray<FPlayloopFeedbackResponse>& Responses,
    const FOnPlayloopFeedbackSubmittedBP& OnDone,
    const FString& RequestId,
    const FString& OriginalSessionId)
{
    const FString SessionId = OriginalSessionId.IsEmpty() ? CurrentSessionId : OriginalSessionId;
    FPlayloopFeedbackResult Failure;
    Failure.FormId = FormId;
    Failure.SessionId = SessionId;

    if (FormId.IsEmpty())
    {
        Failure.ErrorCode = TEXT("invalid_argument");
        Failure.ErrorMessage = TEXT("FormId is required.");
        if (OnDone.IsBound()) OnDone.Execute(Failure);
        return;
    }
    if (SessionId.IsEmpty())
    {
        // Don't try to invent a session id: feedback must tie to a
        // real session so the dashboard can splice answers into the
        // session feed. Caller should Start() before SubmitFeedback.
        Failure.ErrorCode = TEXT("no_session");
        Failure.ErrorMessage = TEXT("Call Start() before SubmitFeedback so a sessionId exists.");
        if (OnDone.IsBound()) OnDone.Execute(Failure);
        return;
    }
    if (Responses.Num() == 0)
    {
        Failure.ErrorCode = TEXT("no_responses");
        Failure.ErrorMessage = TEXT("Submission must include at least one response.");
        if (OnDone.IsBound()) OnDone.Execute(Failure);
        return;
    }

    // Build the wire body. One row per response, all sharing the same
    // submissionId server-side.
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("requestId"), RequestId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens) : RequestId);
    Body->SetStringField(TEXT("formId"), FormId);
    Body->SetStringField(TEXT("sessionId"), SessionId);

    TArray<TSharedPtr<FJsonValue>> ResponsesJson;
    ResponsesJson.Reserve(Responses.Num());
    for (int32 Idx = 0; Idx < Responses.Num(); ++Idx)
    {
        const FPlayloopFeedbackResponse& R = Responses[Idx];
        if (R.FieldId.IsEmpty() || R.Value.IsEmpty())
        {
            Failure.ErrorCode = TEXT("invalid_argument");
            Failure.ErrorMessage = FString::Printf(
                TEXT("Responses[%d] requires non-empty FieldId and Value."), Idx);
            if (OnDone.IsBound()) OnDone.Execute(Failure);
            return;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("fieldId"), R.FieldId);
        Entry->SetStringField(TEXT("value"), R.Value);
        ResponsesJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Body->SetArrayField(TEXT("responses"), ResponsesJson);

    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    const FString Url = FString::Printf(TEXT("%s/api/telemetry/feedback"), *Config.ApiBaseUrl);
    Http->PostJson(Url, Config.ApiKey, Out, Config.Environment, Config.MaxHttpRetries,
        FOnPlayloopHttpDone::CreateLambda([FormId, SessionId, OnDone](FPlayloopHttpResult Result)
        {
            FPlayloopFeedbackResult Out;
            Out.Status = Result.Status;
            Out.FormId = FormId;
            Out.SessionId = SessionId;

            if (Result.Body.IsEmpty())
            {
                Out.bOk = false;
                Out.ErrorCode = TEXT("http_error");
                Out.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Result.Status);
                UE_LOG(LogPlayloop, Log, TEXT("SubmitFeedback failed (status=%d)."), Result.Status);
                if (OnDone.IsBound()) OnDone.Execute(Out);
                return;
            }

            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
            if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
            {
                Out.bOk = false;
                Out.ErrorCode = TEXT("parse_error");
                Out.ErrorMessage = TEXT("Server response was not valid JSON.");
                if (OnDone.IsBound()) OnDone.Execute(Out);
                return;
            }

            bool bServerOk = false;
            Parsed->TryGetBoolField(TEXT("ok"), bServerOk);
            if (!Result.bOk || !bServerOk)
            {
                Out.bOk = false;
                Out.ErrorCode = Parsed->HasField(TEXT("reason"))
                    ? Parsed->GetStringField(TEXT("reason"))
                    : TEXT("http_error");
                Out.ErrorMessage = Parsed->HasField(TEXT("error"))
                    ? Parsed->GetStringField(TEXT("error"))
                    : TEXT("Server rejected the submission.");
                if (OnDone.IsBound()) OnDone.Execute(Out);
                return;
            }

            Out.bOk = true;
            Out.SubmissionId = Parsed->GetStringField(TEXT("submissionId"));
            // formId / sessionId echo back; trust the server when present.
            if (Parsed->HasField(TEXT("formId")))
            {
                Out.FormId = Parsed->GetStringField(TEXT("formId"));
            }
            if (Parsed->HasField(TEXT("sessionId")))
            {
                Out.SessionId = Parsed->GetStringField(TEXT("sessionId"));
            }
            const TArray<TSharedPtr<FJsonValue>>* IdsArray = nullptr;
            if (Parsed->TryGetArrayField(TEXT("responseIds"), IdsArray) && IdsArray)
            {
                for (const TSharedPtr<FJsonValue>& Value : *IdsArray)
                {
                    if (Value.IsValid()) Out.ResponseIds.Add(Value->AsString());
                }
            }
            UE_LOG(LogPlayloop, Log, TEXT("SubmitFeedback ok (submission=%s, fields=%d)."),
                *Out.SubmissionId, Out.ResponseIds.Num());
            if (OnDone.IsBound()) OnDone.Execute(Out);
        }));
}

TArray<FPlayloopFeedbackField> UPlayloopClient::GetDefaultFeedbackFields()
{
    TArray<FPlayloopFeedbackField> Fields;
    Fields.Reserve(3);
    {
        FPlayloopFeedbackField F;
        F.Id = TEXT("q1");
        F.Label = TEXT("How was your session?");
        F.Kind = EPlayloopFeedbackFieldKind::Rating1To5;
        F.bRequired = true;
        Fields.Add(F);
    }
    {
        FPlayloopFeedbackField F;
        F.Id = TEXT("q2");
        F.Label = TEXT("What worked?");
        F.Kind = EPlayloopFeedbackFieldKind::ShortText;
        Fields.Add(F);
    }
    {
        FPlayloopFeedbackField F;
        F.Id = TEXT("q3");
        F.Label = TEXT("What didn't?");
        F.Kind = EPlayloopFeedbackFieldKind::LongText;
        Fields.Add(F);
    }
    return Fields;
}

void UPlayloopClient::SubmitBugReport(
    const FString& Title,
    const FString& Description,
    EPlayloopBugSeverity Severity,
    const FOnPlayloopBugReportSubmittedBP& OnDone)
{
    SubmitBugReportWithContext(Title, Description, Severity, FPlayloopBugReportParams(), OnDone);
}

void UPlayloopClient::SubmitBugReportWithContext(
    const FString& Title,
    const FString& Description,
    EPlayloopBugSeverity Severity,
    const FPlayloopBugReportParams& Params,
    const FOnPlayloopBugReportSubmittedBP& OnDone)
{
    // Capture the live client state the pure logic layer needs. Context
    // auto-fill uses the same sources as telemetry: Config.BuildVersion, the
    // resolved Config.Environment, and SimplePlatformName().
    PlayloopBugReport::FEnv Env;
    Env.bEnabled = !Config.ApiKey.IsEmpty();
    Env.ApiBaseUrl = Config.ApiBaseUrl;
    Env.ResolvedSessionId = CurrentSessionId;
    Env.AutoEnvironment = Config.Environment;
    Env.AutoPlatform = SimplePlatformName();
    Env.AutoBuildVersion = Config.BuildVersion;

    // The real transport: the same PostJson path (with retry/backoff) the rest
    // of the SDK uses. The body is built once by the logic layer, so retries
    // re-send the identical idempotency key.
    TSharedPtr<FPlayloopHttpClient> HttpLocal = Http;
    const FString ApiKeyLocal = Config.ApiKey;
    const FString EnvSlug = Config.Environment;
    const int32 Retries = Config.MaxHttpRetries;
    PlayloopBugReport::FSender Sender =
        [HttpLocal, ApiKeyLocal, EnvSlug, Retries](const FString& Url, const FString& Body, TFunction<void(const FPlayloopHttpResult&)> Done)
        {
            if (!HttpLocal.IsValid())
            {
                FPlayloopHttpResult Failed;
                Failed.bOk = false;
                Done(Failed);
                return;
            }
            HttpLocal->PostJson(Url, ApiKeyLocal, Body, EnvSlug, Retries,
                FOnPlayloopHttpDone::CreateLambda([Done](FPlayloopHttpResult Result) { Done(Result); }));
        };

    PlayloopBugReport::Submit(Title, Description, Severity, Params, Env, Sender,
        [OnDone](const FPlayloopBugReportResult& Result)
        {
            if (OnDone.IsBound()) OnDone.Execute(Result);
        });
}

#include "PlayloopExperiments.h"
#include "PlayloopDeviceId.h"
#include "PlayloopHttpClient.h"
#include "PlayloopSDK.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FPlayloopVariantConfig::Contains(const FString& Key) const
{
    return Values.Contains(Key);
}

int32 FPlayloopVariantConfig::Num() const
{
    return Values.Num();
}

FString FPlayloopVariantConfig::GetString(const FString& Key, const FString& Fallback) const
{
    const FPlayloopConfigValue* Found = Values.Find(Key);
    if (Found && Found->Type == EPlayloopConfigValueType::String)
    {
        return Found->StringValue;
    }
    return Fallback;
}

double FPlayloopVariantConfig::GetNumber(const FString& Key, double Fallback) const
{
    const FPlayloopConfigValue* Found = Values.Find(Key);
    if (Found && Found->Type == EPlayloopConfigValueType::Number)
    {
        return Found->NumberValue;
    }
    return Fallback;
}

int32 FPlayloopVariantConfig::GetInt(const FString& Key, int32 Fallback) const
{
    const FPlayloopConfigValue* Found = Values.Find(Key);
    if (Found && Found->Type == EPlayloopConfigValueType::Number)
    {
        return static_cast<int32>(Found->NumberValue);
    }
    return Fallback;
}

float FPlayloopVariantConfig::GetFloat(const FString& Key, float Fallback) const
{
    const FPlayloopConfigValue* Found = Values.Find(Key);
    if (Found && Found->Type == EPlayloopConfigValueType::Number)
    {
        return static_cast<float>(Found->NumberValue);
    }
    return Fallback;
}

bool FPlayloopVariantConfig::GetBool(const FString& Key, bool bFallback) const
{
    const FPlayloopConfigValue* Found = Values.Find(Key);
    if (Found && Found->Type == EPlayloopConfigValueType::Bool)
    {
        return Found->BoolValue;
    }
    return bFallback;
}

void UPlayloopExperiments::Initialize(const FPlayloopConfig& InConfig, TSharedPtr<FPlayloopHttpClient> InHttp)
{
    Config = InConfig;
    Http = InHttp;

    // A fresh Configure() starts a new session: drop any cached assignments.
    Cache.Reset();
    ConfigCache.Reset();
    bHasCache = false;
    bAttemptedInitialFetch = false;
    RefetchedUnknown.Reset();
    bFetchInFlight = false;
    PendingRefresh.Reset();

    TWeakObjectPtr<UPlayloopExperiments> WeakThis(this);
    Fetcher = [WeakThis](FFetchComplete Complete)
    {
        UPlayloopExperiments* Self = WeakThis.Get();
        if (!Self)
        {
            Complete.ExecuteIfBound(false, FPlayloopAssignmentMap(), FPlayloopVariantConfigMap());
            return;
        }
        Self->FetchViaHttp(Complete);
    };
}

void UPlayloopExperiments::VariantAsync(const FString& ExperimentId, FOnPlayloopVariantResolved OnResolved)
{
    if (ExperimentId.IsEmpty())
    {
        OnResolved.ExecuteIfBound(FString());
        return;
    }

    TWeakObjectPtr<UPlayloopExperiments> WeakThis(this);

    // Runs once the initial fetch has been attempted. Reads the cache and,
    // for an unknown id, spends the one allowed refetch before giving up.
    auto AfterInitial = [WeakThis, ExperimentId, OnResolved]()
    {
        UPlayloopExperiments* Self = WeakThis.Get();
        if (!Self)
        {
            OnResolved.ExecuteIfBound(FString());
            return;
        }

        if (Self->bHasCache)
        {
            if (const FString* Found = Self->Cache.Find(ExperimentId))
            {
                OnResolved.ExecuteIfBound(*Found);
                return;
            }
        }

        // Unknown id: refetch ONCE per id per session, then cache the
        // negative answer so we never loop.
        if (Self->RefetchedUnknown.Contains(ExperimentId))
        {
            OnResolved.ExecuteIfBound(FString());
            return;
        }
        Self->RefetchedUnknown.Add(ExperimentId);
        Self->RefreshInternal(FOnPlayloopExperimentsRefreshed::CreateLambda(
            [WeakThis, ExperimentId, OnResolved]()
            {
                UPlayloopExperiments* S2 = WeakThis.Get();
                if (S2 && S2->bHasCache)
                {
                    if (const FString* Found = S2->Cache.Find(ExperimentId))
                    {
                        OnResolved.ExecuteIfBound(*Found);
                        return;
                    }
                }
                OnResolved.ExecuteIfBound(FString());
            }));
    };

    // Lazy first fetch: only the FIRST VariantAsync() call triggers it.
    if (!bAttemptedInitialFetch)
    {
        RefreshInternal(FOnPlayloopExperimentsRefreshed::CreateLambda(
            [AfterInitial]() { AfterInitial(); }));
    }
    else
    {
        AfterInitial();
    }
}

void UPlayloopExperiments::VariantAsync(const FString& ExperimentId, const FOnPlayloopVariantResolvedBP& OnResolved)
{
    FOnPlayloopVariantResolvedBP Bound = OnResolved;
    VariantAsync(ExperimentId, FOnPlayloopVariantResolved::CreateLambda(
        [Bound](const FString& VariantKey)
        {
            Bound.ExecuteIfBound(VariantKey);
        }));
}

FString UPlayloopExperiments::Variant(const FString& ExperimentId) const
{
    if (bHasCache)
    {
        if (const FString* Found = Cache.Find(ExperimentId))
        {
            return *Found;
        }
    }
    return FString();
}

void UPlayloopExperiments::GetConfigAsync(const FString& ExperimentId, FOnPlayloopVariantConfigResolved OnResolved)
{
    if (ExperimentId.IsEmpty())
    {
        OnResolved.ExecuteIfBound(FPlayloopVariantConfig());
        return;
    }

    TWeakObjectPtr<UPlayloopExperiments> WeakThis(this);

    // Runs once the initial fetch has been attempted. Reads the config cache
    // and, for an unknown id, spends the one allowed refetch before giving up.
    auto AfterInitial = [WeakThis, ExperimentId, OnResolved]()
    {
        UPlayloopExperiments* Self = WeakThis.Get();
        if (!Self)
        {
            OnResolved.ExecuteIfBound(FPlayloopVariantConfig());
            return;
        }

        if (Self->bHasCache)
        {
            if (const FPlayloopVariantConfig* Found = Self->ConfigCache.Find(ExperimentId))
            {
                OnResolved.ExecuteIfBound(*Found);
                return;
            }
        }

        // Unknown id: spend the one allowed refetch (shared with VariantAsync's
        // negative cache), then read whatever config arrived.
        if (Self->RefetchedUnknown.Contains(ExperimentId))
        {
            OnResolved.ExecuteIfBound(FPlayloopVariantConfig());
            return;
        }
        Self->RefetchedUnknown.Add(ExperimentId);
        Self->RefreshInternal(FOnPlayloopExperimentsRefreshed::CreateLambda(
            [WeakThis, ExperimentId, OnResolved]()
            {
                UPlayloopExperiments* S2 = WeakThis.Get();
                if (S2 && S2->bHasCache)
                {
                    if (const FPlayloopVariantConfig* Found = S2->ConfigCache.Find(ExperimentId))
                    {
                        OnResolved.ExecuteIfBound(*Found);
                        return;
                    }
                }
                OnResolved.ExecuteIfBound(FPlayloopVariantConfig());
            }));
    };

    // Lazy first fetch mirrors VariantAsync(): only the first call triggers it.
    if (!bAttemptedInitialFetch)
    {
        RefreshInternal(FOnPlayloopExperimentsRefreshed::CreateLambda(
            [AfterInitial]() { AfterInitial(); }));
    }
    else
    {
        AfterInitial();
    }
}

void UPlayloopExperiments::GetConfigAsync(const FString& ExperimentId, const FOnPlayloopVariantConfigResolvedBP& OnResolved)
{
    FOnPlayloopVariantConfigResolvedBP Bound = OnResolved;
    GetConfigAsync(ExperimentId, FOnPlayloopVariantConfigResolved::CreateLambda(
        [Bound](const FPlayloopVariantConfig& Config)
        {
            Bound.ExecuteIfBound(Config);
        }));
}

FPlayloopVariantConfig UPlayloopExperiments::GetConfig(const FString& ExperimentId) const
{
    if (bHasCache)
    {
        if (const FPlayloopVariantConfig* Found = ConfigCache.Find(ExperimentId))
        {
            return *Found;
        }
    }
    return FPlayloopVariantConfig();
}

void UPlayloopExperiments::RefreshAsync(FOnPlayloopExperimentsRefreshed OnRefreshed)
{
    RefreshInternal(OnRefreshed);
}

void UPlayloopExperiments::RefreshAsync(const FOnPlayloopExperimentsRefreshedBP& OnRefreshed)
{
    FOnPlayloopExperimentsRefreshedBP Bound = OnRefreshed;
    RefreshInternal(FOnPlayloopExperimentsRefreshed::CreateLambda(
        [Bound]() { Bound.ExecuteIfBound(); }));
}

TMap<FString, FString> UPlayloopExperiments::Snapshot() const
{
    return bHasCache ? Cache : TMap<FString, FString>();
}

void UPlayloopExperiments::RefreshInternal(FOnPlayloopExperimentsRefreshed OnDone)
{
    if (OnDone.IsBound())
    {
        PendingRefresh.Add(OnDone);
    }

    // Coalesce concurrent refreshes onto the single in-flight request.
    if (bFetchInFlight)
    {
        return;
    }

    if (!Fetcher)
    {
        // No fetcher configured (Initialize never ran). Treat as an
        // attempted-but-empty fetch so VariantAsync resolves to empty
        // rather than hanging.
        bAttemptedInitialFetch = true;
        FlushPendingRefresh();
        return;
    }

    bFetchInFlight = true;
    TWeakObjectPtr<UPlayloopExperiments> WeakThis(this);
    Fetcher(FFetchComplete::CreateLambda(
        [WeakThis](bool bOk, const FPlayloopAssignmentMap& Assignments, const FPlayloopVariantConfigMap& ConfigMap)
        {
            UPlayloopExperiments* Self = WeakThis.Get();
            if (!Self) return;
            Self->bAttemptedInitialFetch = true;
            if (bOk)
            {
                // Replace (not merge): the server map is authoritative for
                // the session; a stopped experiment drops out of the cache.
                Self->Cache = Assignments;
                // The parallel config map is optional; an older server that
                // omits it leaves ConfigCache empty, so GetConfig returns an
                // empty config (backward-compatible).
                Self->ConfigCache = ConfigMap;
                Self->bHasCache = true;
            }
            // On failure: last-good wins. Keep whatever cache we have
            // (possibly none on a first-fetch failure). Never throw.
            Self->bFetchInFlight = false;
            Self->FlushPendingRefresh();
        }));
}

void UPlayloopExperiments::FlushPendingRefresh()
{
    TArray<FOnPlayloopExperimentsRefreshed> Callbacks = MoveTemp(PendingRefresh);
    PendingRefresh.Reset();
    for (FOnPlayloopExperimentsRefreshed& Cb : Callbacks)
    {
        Cb.ExecuteIfBound();
    }
}

void UPlayloopExperiments::FetchViaHttp(FFetchComplete Complete)
{
    if (!Http.IsValid())
    {
        Complete.ExecuteIfBound(false, FPlayloopAssignmentMap(), FPlayloopVariantConfigMap());
        return;
    }

    // The ingest key identifies the game server-side, so experiments no longer
    // need a gameId. The server scopes the lookup to the key's game. We send
    // only the device id.
    const FString DeviceId = FPlayloopDeviceId::Resolve();
    FString Url = FString::Printf(
        TEXT("%s/api/telemetry/experiments?deviceId=%s"),
        *Config.ApiBaseUrl,
        *FGenericPlatformHttp::UrlEncode(DeviceId));
    if (!Config.BuildVersion.IsEmpty())
    {
        Url += FString::Printf(TEXT("&buildVersion=%s"),
            *FGenericPlatformHttp::UrlEncode(Config.BuildVersion));
    }

    Http->GetJson(Url, Config.ApiKey, Config.Environment, Config.MaxHttpRetries,
        FOnPlayloopHttpDone::CreateLambda([Complete](FPlayloopHttpResult Result)
        {
            if (!Result.bOk || Result.Body.IsEmpty())
            {
                Complete.ExecuteIfBound(false, FPlayloopAssignmentMap(), FPlayloopVariantConfigMap());
                return;
            }
            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
            if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
            {
                Complete.ExecuteIfBound(false, FPlayloopAssignmentMap(), FPlayloopVariantConfigMap());
                return;
            }

            TMap<FString, FString> Map;
            const TSharedPtr<FJsonObject>* Experiments = nullptr;
            if (Parsed->TryGetObjectField(TEXT("experiments"), Experiments) && Experiments && Experiments->IsValid())
            {
                for (const auto& Pair : (*Experiments)->Values)
                {
                    FString Value;
                    if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
                    {
                        Map.Emplace(FString(Pair.Key), Value);
                    }
                }
            }

            // Parse the optional parallel `config` map: { experimentId: { key:
            // primitive } }. Keep only primitive values (string / number /
            // bool); anything else (an array, a nested object, null) is dropped
            // defensively. Absent for older experiments or an older server.
            FPlayloopVariantConfigMap ConfigMap;
            const TSharedPtr<FJsonObject>* ConfigField = nullptr;
            if (Parsed->TryGetObjectField(TEXT("config"), ConfigField) && ConfigField && ConfigField->IsValid())
            {
                for (const auto& ExpPair : (*ConfigField)->Values)
                {
                    if (!ExpPair.Value.IsValid() || ExpPair.Value->Type != EJson::Object)
                    {
                        continue;
                    }
                    const TSharedPtr<FJsonObject>& Entry = ExpPair.Value->AsObject();
                    if (!Entry.IsValid()) continue;
                    FPlayloopVariantConfig VariantConfig;
                    for (const auto& Kv : Entry->Values)
                    {
                        if (!Kv.Value.IsValid()) continue;
                        FPlayloopConfigValue ConfigValue;
                        switch (Kv.Value->Type)
                        {
                            case EJson::String:
                                ConfigValue.Type = EPlayloopConfigValueType::String;
                                ConfigValue.StringValue = Kv.Value->AsString();
                                break;
                            case EJson::Number:
                                ConfigValue.Type = EPlayloopConfigValueType::Number;
                                ConfigValue.NumberValue = Kv.Value->AsNumber();
                                break;
                            case EJson::Boolean:
                                ConfigValue.Type = EPlayloopConfigValueType::Bool;
                                ConfigValue.BoolValue = Kv.Value->AsBool();
                                break;
                            default:
                                // Non-primitive (array / object / null): skip.
                                continue;
                        }
                        VariantConfig.Values.Emplace(FString(Kv.Key), MoveTemp(ConfigValue));
                    }
                    if (VariantConfig.Values.Num() > 0)
                    {
                        ConfigMap.Emplace(FString(ExpPair.Key), MoveTemp(VariantConfig));
                    }
                }
            }

            // A well-formed response with no running experiments is a
            // successful empty fetch.
            Complete.ExecuteIfBound(true, Map, ConfigMap);
        }));
}

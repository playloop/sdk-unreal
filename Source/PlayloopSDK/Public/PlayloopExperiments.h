#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PlayloopTypes.h"
#include "PlayloopExperiments.generated.h"

class FPlayloopHttpClient;

/**
 * Server-resolved { experimentId: variantKey } assignment map. Aliased so the
 * comma in TMap<,> doesn't break the function-like delegate macros below. A
 * non-variadic macro would otherwise read it as two arguments.
 */
using FPlayloopAssignmentMap = TMap<FString, FString>;

/** The primitive kind a variant config value holds. */
UENUM(BlueprintType)
enum class EPlayloopConfigValueType : uint8
{
    String,
    Number,
    Bool,
};

/**
 * One value inside a variant's config payload. Primitives only: a string, a
 * number (stored as a double), or a bool. Read it through
 * FPlayloopVariantConfig's typed getters rather than the fields directly.
 */
USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopConfigValue
{
    GENERATED_BODY()

    /** Which field below carries the value. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Experiments")
    EPlayloopConfigValueType Type = EPlayloopConfigValueType::String;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Experiments")
    FString StringValue;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Experiments")
    double NumberValue = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Experiments")
    bool BoolValue = false;
};

/**
 * A variant's optional runtime config payload: a flat map of primitive values
 * (string / number / bool) the dashboard attaches to the assigned variant.
 * Lets you tweak a tuning number, a feature flag, or a label from the
 * dashboard without shipping a new build. Read values through the typed
 * getters, each of which returns a fallback when the key is missing or the
 * stored value is not the requested type. An empty config (Num() == 0) means
 * the assigned variant carries no payload. Primitives only, by design: no
 * arrays and no nested objects.
 */
USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopVariantConfig
{
    GENERATED_BODY()

    /** Raw values keyed by config key. Prefer the typed getters below. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Experiments")
    TMap<FString, FPlayloopConfigValue> Values;

    /** True when Key is present. */
    bool Contains(const FString& Key) const;

    /** Number of config values present. */
    int32 Num() const;

    /** Read a string value, or Fallback when missing / not a string. */
    FString GetString(const FString& Key, const FString& Fallback = FString()) const;

    /** Read a numeric value as a double, or Fallback when missing / not a number. */
    double GetNumber(const FString& Key, double Fallback = 0.0) const;

    /** Read a numeric value as an int32 (truncating), or Fallback. */
    int32 GetInt(const FString& Key, int32 Fallback = 0) const;

    /** Read a numeric value as a float, or Fallback. */
    float GetFloat(const FString& Key, float Fallback = 0.0f) const;

    /** Read a bool value, or bFallback when missing / not a bool. */
    bool GetBool(const FString& Key, bool bFallback = false) const;
};

/**
 * Parallel { experimentId: FPlayloopVariantConfig } map, carrying each
 * assigned variant's optional config payload. Aliased for the same
 * macro-comma reason as FPlayloopAssignmentMap.
 */
using FPlayloopVariantConfigMap = TMap<FString, FPlayloopVariantConfig>;

/**
 * Fires with the resolved variant key. An empty string means "no variant"
 * (experiment unknown / not running / first fetch failed), the Unreal
 * stand-in for the `null` the TypeScript SDK returns.
 */
DECLARE_DELEGATE_OneParam(FOnPlayloopVariantResolved, const FString& /*VariantKey*/);

/** Fires once a refresh (or the lazy first fetch) has completed. */
DECLARE_DELEGATE(FOnPlayloopExperimentsRefreshed);

// Blueprint-facing callbacks for the async methods below. These are single-cast
// dynamic delegates (not multicast): UHT only accepts single-cast dynamic
// delegates as BlueprintCallable function parameters, and each fires exactly
// once per call anyway.

/** Blueprint-facing variant delegate. Empty string == "no variant". */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopVariantResolvedBP, const FString&, VariantKey);

/** Blueprint-facing refresh-complete delegate. */
DECLARE_DYNAMIC_DELEGATE(FOnPlayloopExperimentsRefreshedBP);

/**
 * Fires with the assigned variant's config payload. An empty config
 * (Num() == 0) means "no config" (experiment unknown / not running / the
 * assigned variant carries no payload), the Unreal stand-in for the `null`
 * the TypeScript SDK returns.
 */
DECLARE_DELEGATE_OneParam(FOnPlayloopVariantConfigResolved, const FPlayloopVariantConfig& /*Config*/);

/** Blueprint-facing config delegate. An empty config == "no config". */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopVariantConfigResolvedBP, const FPlayloopVariantConfig&, Config);

/**
 * A/B experiments: SDK side. Mirrors the TypeScript SDK's
 * `client.experiments` surface in Unreal idioms.
 *
 * Variant evaluation is server-side: this object fetches the player's
 * pre-resolved `{ experimentId: variantKey }` map from
 * `GET /api/telemetry/experiments` and caches it for the lifetime of the
 * owning client (one game session). `VariantAsync(id)` reads from that
 * cache. Four behaviors:
 *
 *   - **Lazy fetch.** The map is fetched on the FIRST VariantAsync() call,
 *     not at Configure(). Games that never run experiments never pay the
 *     network cost. Opt into eager startup fetch with
 *     `PlayloopConfig.bPrefetchExperiments = true`.
 *   - **Session-scoped cache.** The cache holds for the client's lifetime.
 *     Variant stability across a playthrough beats freshness. Call
 *     RefreshAsync() for manual control in long-running sessions.
 *   - **Refetch-once on unknown id.** VariantAsync('exp_new') for an id not
 *     in the cache triggers exactly ONE refetch per unknown id per session.
 *     If it's still unknown after the refetch, the answer is empty and that
 *     negative is cached. No retry loops.
 *   - **Last-good on network error.** A failed fetch never blocks: if a
 *     cached map exists, its answers stand; if the very first fetch failed,
 *     VariantAsync resolves to an empty string.
 *
 * VariantAsync() is on the game's hot path. It is async, side-effect-free
 * for the caller, and never throws. Synchronous Variant() returns the
 * cached answer only (empty if the cache hasn't loaded yet); use it on
 * performance-critical paths where you know assignments are already warm.
 */
// `final`: this is an internal leaf object (owned by UPlayloopClient, never
// subclassed). It also sidesteps UE 5.8's constexpr DataSizeOf<>/offsetof on a
// polymorphic UObject with a TFunction member. The is_final path uses sizeof.
UCLASS(BlueprintType)
class PLAYLOOPSDK_API UPlayloopExperiments final : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Bind config + the shared HTTP client. Called by UPlayloopClient in
     * Configure(); installs the default network fetcher. Re-callable.
     * A fresh Configure() resets the session cache.
     */
    void Initialize(const FPlayloopConfig& InConfig, TSharedPtr<FPlayloopHttpClient> InHttp);

    /**
     * Resolve the player's variant for ExperimentId. Async, never throws.
     * Resolves to an empty string when the experiment is unknown / not
     * running / the first fetch failed. See the class docstring for the
     * full contract. (C++ delegate variant: use the Blueprint overload
     * from BP graphs.)
     */
    void VariantAsync(const FString& ExperimentId, FOnPlayloopVariantResolved OnResolved);

    /**
     * Blueprint-callable wrapper around VariantAsync. Binds the OnResolved
     * event and fires it once with the variant key (empty string == "no
     * variant").
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments", meta = (AutoCreateRefTerm = "OnResolved", DisplayName = "Variant Async"))
    void VariantAsync(const FString& ExperimentId, const FOnPlayloopVariantResolvedBP& OnResolved);

    /**
     * Synchronous, cached-only variant lookup. Returns the cached key, or
     * an empty string on a cache miss. Never triggers a fetch: the "I know
     * it's already loaded" optimization for hot paths.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments")
    FString Variant(const FString& ExperimentId) const;

    /**
     * Resolve the assigned variant's config payload for ExperimentId: a flat
     * map of primitive values (string / number / bool) set from the dashboard,
     * read through FPlayloopVariantConfig's typed getters. Resolves to an empty
     * config (Num() == 0) when the experiment is unknown / not running, or when
     * the assigned variant carries no config. Async, never throws, and shares
     * the same lazy fetch + session cache as VariantAsync. (C++ delegate
     * variant: use the Blueprint overload from BP graphs.)
     */
    void GetConfigAsync(const FString& ExperimentId, FOnPlayloopVariantConfigResolved OnResolved);

    /**
     * Blueprint-callable wrapper around GetConfigAsync. Binds the OnResolved
     * event and fires it once with the config (an empty config == "no config").
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments", meta = (AutoCreateRefTerm = "OnResolved", DisplayName = "Get Config Async"))
    void GetConfigAsync(const FString& ExperimentId, const FOnPlayloopVariantConfigResolvedBP& OnResolved);

    /**
     * Synchronous, cached-only config lookup. Returns the cached config, or an
     * empty config on a cache miss. Never triggers a fetch: the "I know it's
     * already loaded" optimization for hot paths, paired with Variant().
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments")
    FPlayloopVariantConfig GetConfig(const FString& ExperimentId) const;

    /**
     * Force a re-fetch of the assignment map. Useful in long-running
     * sessions when an experiment is created or its allocations change
     * mid-play. Never throws. A failed refresh leaves the last-good cache
     * in place. Concurrent calls share one request. (C++ delegate variant.)
     */
    void RefreshAsync(FOnPlayloopExperimentsRefreshed OnRefreshed = FOnPlayloopExperimentsRefreshed());

    /** Blueprint-callable wrapper around RefreshAsync. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments", meta = (AutoCreateRefTerm = "OnRefreshed", DisplayName = "Refresh Async"))
    void RefreshAsync(const FOnPlayloopExperimentsRefreshedBP& OnRefreshed);

    /**
     * Copy of the current assignment map. Empty when nothing has been
     * fetched yet. The client reads this to tag the first telemetry flush's
     * experimentTags; the server re-validates and persists only
     * the matches.
     */
    TMap<FString, FString> Snapshot() const;

    /**
     * Test seam: replace the network fetch. The provided fetcher performs
     * one fetch and invokes Complete(bOk, Assignments) when done. It may
     * complete synchronously, which keeps automation specs deterministic.
     */
    DECLARE_DELEGATE_ThreeParams(FFetchComplete, bool /*bOk*/, const FPlayloopAssignmentMap& /*Assignments*/, const FPlayloopVariantConfigMap& /*Config*/);
    using FFetcher = TFunction<void(FFetchComplete)>;
    void SetFetcherForTesting(FFetcher InFetcher) { Fetcher = MoveTemp(InFetcher); }

private:
    void RefreshInternal(FOnPlayloopExperimentsRefreshed OnDone);
    void FlushPendingRefresh();
    void FetchViaHttp(FFetchComplete Complete);

    /** Server-resolved assignment map. Only meaningful once bHasCache. */
    TMap<FString, FString> Cache;

    /**
     * Parallel map of each assigned variant's optional config payload, keyed
     * by experiment id. Filled from the same fetch as Cache. An experiment
     * whose variant has no config simply has no entry, so GetConfig() returns
     * an empty config. Only meaningful once bHasCache.
     */
    TMap<FString, FPlayloopVariantConfig> ConfigCache;

    /** True once a SUCCESSFUL fetch has populated Cache (null distinction). */
    bool bHasCache = false;

    /** True once the lazy first fetch has been ATTEMPTED (success or fail). */
    bool bAttemptedInitialFetch = false;

    /** Ids we've already spent the one allowed refetch on (negative cache). */
    TSet<FString> RefetchedUnknown;

    /** Dedupes concurrent fetches into a single in-flight request. */
    bool bFetchInFlight = false;
    TArray<FOnPlayloopExperimentsRefreshed> PendingRefresh;

    FPlayloopConfig Config;
    TSharedPtr<FPlayloopHttpClient> Http;
    FFetcher Fetcher;
};

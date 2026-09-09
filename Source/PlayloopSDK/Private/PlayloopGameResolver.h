#pragma once

#include "CoreMinimal.h"

class FPlayloopHttpClient;

/** The game identity an ingest key resolves to (from GET /api/telemetry/resolve). */
struct FPlayloopResolvedGame
{
    FString GameId;
    FString Slug;
    FString Name;
};

/** Fires once a resolve attempt settles. bOk is true on success; Game is the
 *  resolved identity (empty fields on failure). */
DECLARE_DELEGATE_TwoParams(FOnPlayloopGameResolved, bool /*bOk*/, const FPlayloopResolvedGame& /*Game*/);

/**
 * Resolves the game identity from the ingest key.
 *
 * The per-game ingest key already identifies the game server-side, so the SDK
 * no longer asks the dev for a slug or id. Instead it lazily hits
 * GET /api/telemetry/resolve (auth: Bearer <ingestKey>) once and caches the
 * result for the lifetime of the owning client.
 *
 * Contract (mirrors the TypeScript SDK's GameResolver):
 *
 *   - Lazy + cached. The fetch fires on the first ResolveAsync() call (or a
 *     constructor-time warm-up via Prefetch()). Concurrent callers coalesce
 *     onto one in-flight request; the result is cached for the client's
 *     lifetime.
 *   - Non-blocking + never fails the caller. Telemetry never awaits this. The
 *     key attributes the session server-side already. Consumers that DO need
 *     the game identity (event-config, tester-keys) await ResolveAsync(), which
 *     reports bOk=false with empty fields on any failure (offline, invalid key,
 *     bad shape). The failure is cached so we don't retry on the hot path.
 *
 * Thread model matches FPlayloopHttpClient: the HTTP callback fires on the game
 * thread, so the cached fields and the in-flight flag need no lock.
 */
class PLAYLOOPSDK_API FPlayloopGameResolver : public TSharedFromThis<FPlayloopGameResolver, ESPMode::ThreadSafe>
{
public:
    FPlayloopGameResolver(
        TSharedPtr<FPlayloopHttpClient> InHttp,
        const FString& InApiKey,
        const FString& InApiBaseUrl,
        const FString& InEnvironment,
        int32 InMaxRetries);

    /**
     * Resolve the game identity from the ingest key. Cached after the first
     * attempt; never fails the caller (reports bOk=false on any error). The
     * callback fires on the game thread: synchronously when the result is
     * already cached.
     */
    void ResolveAsync(FOnPlayloopGameResolved OnDone);

    /**
     * Fire-and-forget warm-up so the round-trip overlaps host-app startup.
     * The next ResolveAsync() reads the cached (possibly empty) result.
     */
    void Prefetch();

    /** Test seam: replace the network resolve with a synchronous stub. */
    DECLARE_DELEGATE_OneParam(FResolveComplete, const FPlayloopResolvedGame& /*Game*/);
    using FResolver = TFunction<void(FResolveComplete)>;
    void SetResolverForTesting(FResolver InResolver) { Resolver = MoveTemp(InResolver); }

private:
    void DoResolve();
    void FlushPending(bool bOk, const FPlayloopResolvedGame& Game);
    void ResolveViaHttp(FResolveComplete Complete);

    TSharedPtr<FPlayloopHttpClient> Http;
    FString ApiKey;
    FString ApiBaseUrl;
    FString Environment;
    int32 MaxRetries = 3;

    /** The resolved game; only meaningful once bResolved && bResolvedOk. */
    FPlayloopResolvedGame Result;
    /** True once the first attempt settled (success OR failure). */
    bool bResolved = false;
    /** True when the cached attempt succeeded. */
    bool bResolvedOk = false;

    bool bInFlight = false;
    TArray<FOnPlayloopGameResolved> Pending;

    FResolver Resolver;
};

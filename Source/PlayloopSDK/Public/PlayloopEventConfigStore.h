#pragma once

#include "CoreMinimal.h"

class FPlayloopHttpClient;
class FPlayloopGameResolver;

/**
 * The subset of a server event-config row the runtime SDK acts on.
 * `HideFromAi` is forwarded for completeness but the SDK does not act
 * on it at runtime (it's a server-side digest concern).
 */
struct FPlayloopEventConfigEntry
{
    FString EventName;
    /** Drop the event before it enters the telemetry buffer. */
    bool bSdkIgnore = false;
    /** Merge the event's data into the client state accumulator on every fire. */
    bool bLinkToSummary = false;
    /** Server-side AI digest exclusion. Forwarded; not acted on at runtime. */
    bool bHideFromAi = false;
    FString Category;
};

/**
 * Per-game settings block returned alongside `events` from the
 * event-config GET. Defaults are applied when the response omits the
 * block (older deploy) so callers always read a usable shape.
 */
struct FPlayloopGameConfigBlock
{
    /** From the server's `summaryEventName`: the snapshot/summary event name. */
    FString SnapshotEventName;
    bool bCrashesEnabled = true;
    int32 CrashRecentEventsCount = 5;
    double CrashSampleRate = 1.0;
    /** Whether the in-game feedback widget renders the "Powered by Playloop" mark. */
    bool bShowBranding = true;
};

/**
 * Per-event SDK-side config. Pulls per-event flags from
 * `GET /api/v1/games/<slug>/event-config` and exposes a sync
 * `Lookup(eventName)` the telemetry layer consults on every `Track()`
 * call, plus a `GameSettings()` getter for the per-game block (read by
 * the feedback widget for the branding gate + the crash handler).
 *
 * The game slug is resolved from the ingest key via `FPlayloopGameResolver`.
 * The dev no longer configures a slug. When the key can't be resolved
 * (offline / invalid key) the store stays inert and every `Lookup()`
 * short-circuits, equivalent to a fresh config with no rows.
 *
 * Mirrors `EventConfigStore.cs` (Unity) / `event_config_store.gd`
 * (Godot). Thread-safe: the cache is guarded by a critical section,
 * and the HTTP callback fires on the game thread (same contract as
 * `FPlayloopHttpClient`).
 */
class PLAYLOOPSDK_API FPlayloopEventConfigStore : public TSharedFromThis<FPlayloopEventConfigStore, ESPMode::ThreadSafe>
{
public:
    FPlayloopEventConfigStore(TSharedPtr<FPlayloopHttpClient> InHttp, TSharedPtr<FPlayloopGameResolver> InResolver, const FString& InApiKey, const FString& InApiBaseUrl, const FString& InEnvironment, int32 InMaxRetries);

    /**
     * Look up the per-event config row. Returns false when there's no
     * row for this event (default-do-nothing) or when the store hasn't
     * fetched yet. Callers treat "no entry" as "flow through normally".
     */
    bool Lookup(const FString& EventName, FPlayloopEventConfigEntry& OutEntry) const;

    /** Per-game settings block. Always returns a usable shape (defaults pre-fetch). */
    FPlayloopGameConfigBlock GameSettings() const;

    /** True once the first successful fetch has populated the cache. */
    bool IsLoaded() const;

    /**
     * Fire-and-forget fetch that replaces the local cache. Resolves the game
     * slug from the ingest key first; no-ops when the key can't be resolved.
     * Concurrent calls are de-duped via a single-flight guard. Safe to call
     * from Start() / Configure().
     */
    void Refresh();

private:
    FString ResolveUrl(const FString& Slug) const;
    void FetchWithSlug(const FString& Slug);

    TSharedPtr<FPlayloopHttpClient> Http;
    TSharedPtr<FPlayloopGameResolver> Resolver;
    FString ApiKey;
    FString ApiBaseUrl;
    FString Environment;
    int32 MaxRetries = 3;

    mutable FCriticalSection Mutex;
    TMap<FString, FPlayloopEventConfigEntry> Entries;
    FPlayloopGameConfigBlock Game;
    bool bFetched = false;
    bool bRefreshing = false;
};

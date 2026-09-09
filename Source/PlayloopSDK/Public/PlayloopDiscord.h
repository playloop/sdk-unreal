#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PlayloopTypes.h"
#include "PlayloopDiscord.generated.h"

class FPlayloopHttpClient;

/**
 * Result of a Discord ingest call. `bOk` is true for any 2xx (the server
 * answers 201 when a new session shell is created and 200 when the post is
 * appended to an open session). `SessionsCreated` is 1 on a fresh session,
 * 0 on an append. `MessagesIngested` is reserved for forward-compatibility.
 * The SDK payload carries no message bodies, so it is 0 today.
 */
USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopDiscordIngestResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    int32 SessionsCreated = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    int32 MessagesIngested = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    int32 Status = 0;

    /** Populated on failure (`invalid_argument`, `http_error`, `parse_error`). Empty on success. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Discord")
    FString ErrorMessage;
};

/** Fires once an Ingest call completes (C++ delegate variant). */
DECLARE_DELEGATE_OneParam(FOnPlayloopDiscordIngested, const FPlayloopDiscordIngestResult& /*Result*/);

/** Blueprint-facing ingest-complete delegate. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopDiscordIngestedBP, const FPlayloopDiscordIngestResult&, Result);

/**
 * Discord ingest: SDK side. Mirrors the TypeScript SDK's `client.discord`
 * surface (and the Python / Unity / Godot equivalents) in Unreal idioms.
 *
 * A single operation, `IngestAsync(channelId, game, threadId?)`, relays a
 * Discord channel (and optional thread) into Playloop so messages from that
 * conversation are folded into a playtest session keyed on the channel /
 * thread. The server creates one session shell per channel/thread window
 * (returning `sessionsCreated: 1`) and appends to it while the window stays
 * open (`sessionsCreated: 0`).
 *
 * Auth note: this endpoint is scoped to a **management** key, not the
 * write-only ingest key used for telemetry. It's intended for a trusted
 * relay (a bot / server bridge), not an end-user game build. Set
 * `PlayloopConfig.ApiKey` to a management key when calling Ingest, and keep
 * that key off the client. It also requires the Discord relay secret,
 * sent as the `x-playloop-secret` header. set it on the client
 * (`PlayloopConfig.RelaySecret`) or per call (IngestAsync's `RelaySecret`
 * parameter). Copy it from Connections > Discord on playloop.gg (it's shown
 * once; rotate it there any time). The call faithfully mirrors the other
 * SDKs: it sends `Authorization: Bearer <ApiKey>` with the snake_case body
 * and never throws. Failures arrive as a result with `bOk = false`.
 */
UCLASS(BlueprintType)
class PLAYLOOPSDK_API UPlayloopDiscord : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Bind config + the shared HTTP client. Called by UPlayloopClient in
     * Configure(); installs the default network fetcher. Re-callable.
     */
    void Initialize(const FPlayloopConfig& InConfig, TSharedPtr<FPlayloopHttpClient> InHttp);

    /**
     * Relay a Discord channel into Playloop. `ChannelId` and `Game` (slug or
     * id) are required; `ThreadId` is optional and omitted from the wire body
     * when empty. `RelaySecret` is the Discord relay secret for this call,
     * sent as the x-playloop-secret header on Discord ingest calls; leave it
     * empty to use the client-level `PlayloopConfig.RelaySecret`. Copy it
     * from Connections > Discord on playloop.gg (it's shown once; rotate it
     * there any time). Async, never throws. The OnDone delegate fires with
     * the parsed result (or a `bOk = false` failure). (C++ delegate variant:
     * use the Blueprint overload from BP graphs.)
     */
    void IngestAsync(
        const FString& ChannelId,
        const FString& Game,
        const FString& ThreadId,
        const FString& RelaySecret,
        FOnPlayloopDiscordIngested OnDone);

    /**
     * Blueprint-callable wrapper around IngestAsync. Leave `ThreadId` empty to
     * omit it. Leave `RelaySecret` empty to use the client-level
     * `PlayloopConfig.RelaySecret` (see the C++ variant's doc for what the
     * relay secret is). The OnDone event fires once with the parsed result.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Discord", meta = (AutoCreateRefTerm = "OnDone", AdvancedDisplay = "ThreadId,RelaySecret", DisplayName = "Ingest Async"))
    void IngestAsync(
        const FString& ChannelId,
        const FString& Game,
        const FString& ThreadId,
        const FString& RelaySecret,
        const FOnPlayloopDiscordIngestedBP& OnDone);

    /**
     * Test seam: replace the network POST. The provided fetcher receives the
     * serialized request body plus the resolved relay secret (per-call value
     * when given, otherwise the client-level config; empty when neither is
     * set. the x-playloop-secret header is omitted in that case) and invokes
     * Complete(bOk, status, responseBody) when done. It may complete
     * synchronously, which keeps automation specs deterministic and
     * network-free.
     */
    DECLARE_DELEGATE_ThreeParams(FFetchComplete, bool /*bOk*/, int32 /*Status*/, const FString& /*ResponseBody*/);
    using FFetcher = TFunction<void(const FString& /*Body*/, const FString& /*RelaySecret*/, FFetchComplete)>;
    void SetFetcherForTesting(FFetcher InFetcher) { Fetcher = MoveTemp(InFetcher); }

private:
    void PostViaHttp(const FString& Body, const FString& RelaySecret, FFetchComplete Complete);

    FPlayloopConfig Config;
    TSharedPtr<FPlayloopHttpClient> Http;
    FFetcher Fetcher;
};

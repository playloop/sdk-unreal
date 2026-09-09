#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Dom/JsonObject.h"
// UE 5.8 tightened transitive includes (IWYU): FTimerHandle is no longer
// pulled in by CoreMinimal/Engine headers, and it's used as a member below,
// so include its defining header explicitly.
#include "Engine/TimerHandle.h"
#include "PlayloopTypes.h"
#include "PlayloopClient.generated.h"

class FPlayloopEventQueue;
class FPlayloopHttpClient;
class FPlayloopEventConfigStore;
class FPlayloopGameResolver;
class FPlayloopAutoInstrument;
struct FPlayloopGameConfigBlock;
class UPlayloopExperiments;
class UPlayloopDiscord;

/**
 * Main runtime client. Construct via NewObject<UPlayloopClient>(),
 * call Configure() with FPlayloopConfig (or rely on UPlayloopSettings),
 * then Start(). The auto-start path runs through
 * UPlayloopSubsystem on GameInstance init.
 *
 * Public API mirrors the other 4 SDKs as closely as Unreal idioms allow.
 * Every method is BlueprintCallable so it's reachable from BP graphs.
 */
UCLASS(BlueprintType)
class PLAYLOOPSDK_API UPlayloopClient : public UObject
{
    GENERATED_BODY()

public:
    UPlayloopClient();
    virtual ~UPlayloopClient() override;

    // -------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------

    /** Apply runtime config. Call before Start(). */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void Configure(const FPlayloopConfig& InConfig);

    /** Begin session: emit session_start, install timers. Idempotent. */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void Start();

    /** Stop timers, flush, clear session. Idempotent. */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void Stop();

    /** Force-flush any pending events immediately. */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void Flush();

    /** Emit session_summary, flush, then Stop(). */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void EndSession();

    // -------------------------------------------------------------
    // A/B experiments
    // -------------------------------------------------------------

    /**
     * The experiments API for this client. Read a player's variant with
     * Experiments()->VariantAsync(id). Lazily created and bound to the
     * client's config + HTTP client. The ingest key identifies the game, so
     * no GameId is needed.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Experiments")
    UPlayloopExperiments* Experiments();

    // -------------------------------------------------------------
    // Discord ingest
    // -------------------------------------------------------------

    /**
     * The Discord ingest API for this client. Relay a Discord channel into a
     * playtest session with
     * Discord()->IngestAsync(channelId, game, threadId, relaySecret).
     * Lazily created and bound to the client's config + HTTP client. Note this
     * endpoint expects a management-scope key plus the Discord relay secret
     * (client-level `FPlayloopConfig.RelaySecret` or per call). see
     * UPlayloopDiscord.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Discord")
    UPlayloopDiscord* Discord();

    // -------------------------------------------------------------
    // Telemetry
    // -------------------------------------------------------------

    /** Track a custom event. JsonProps is a JSON string; pass "{}" for no props. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Telemetry", meta = (AdvancedDisplay = "JsonProps"))
    void Track(const FString& EventName, const FString& JsonProps);

    /** Track an event from C++ with a structured payload. */
    void TrackJson(const FString& EventName, TSharedPtr<FJsonObject> Props);

    // -------------------------------------------------------------
    // State accumulator (reported on session_heartbeat + session_summary)
    // -------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "Playloop|State")
    void SetStateBool(const FString& Key, bool Value);

    UFUNCTION(BlueprintCallable, Category = "Playloop|State")
    void SetStateNumber(const FString& Key, double Value);

    UFUNCTION(BlueprintCallable, Category = "Playloop|State")
    void SetStateString(const FString& Key, const FString& Value);

    UFUNCTION(BlueprintCallable, Category = "Playloop|State")
    void IncrementState(const FString& Key, double Delta = 1.0);

    UFUNCTION(BlueprintCallable, Category = "Playloop|State")
    void ClearState();

    // -------------------------------------------------------------
    // Tester keys
    // -------------------------------------------------------------

    /**
     * Bind this device to a tester handle. ClaimToken is `pl_tt_…` from the
     * redeem flow. The optional `OnDone` delegate fires with the parsed
     * `FPlayloopLinkResult` once the request completes (`bAlreadyLinked` is
     * true when the same device re-runs the same token).
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|TesterKeys", meta = (AutoCreateRefTerm = "OnDone"))
    void LinkTester(const FString& ClaimToken, const FOnPlayloopTesterLinked& OnDone);

    /**
     * Read the currently-linked tester for this device, if any. The optional
     * `OnDone` delegate fires with the parsed `FPlayloopTesterStatus`.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|TesterKeys", meta = (AutoCreateRefTerm = "OnDone"))
    void GetCurrentTester(const FOnPlayloopTesterStatus& OnDone);

    /**
     * Remove a tester binding. Pass the SAME ClaimToken used for LinkTester.
     * The server requires it to authorize the unlink. The optional `OnDone`
     * delegate fires with the parsed `FPlayloopUnlinkResult`.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|TesterKeys", meta = (AutoCreateRefTerm = "OnDone"))
    void UnlinkTester(const FString& ClaimToken, const FOnPlayloopTesterUnlinked& OnDone);

    // -------------------------------------------------------------
    // Player Feedback (free; multi-field forms)
    // -------------------------------------------------------------

    /**
     * Submit a multi-field Player Feedback response.
     *
     * `FormId` is the `ff_...` id from the dashboard form at
     * `/games/[slug]/feedback`. `Responses` is one entry per field
     * the player answered (must contain at least one). The optional
     * `OnDone` delegate fires with the parsed `FPlayloopFeedbackResult`
     * once the request completes; pass an empty delegate when you
     * don't care about the result.
     *
     * Persist RequestId and OriginalSessionId for retries after restarting.
     * Omit OriginalSessionId for a new note in the current session.
     *
     * Player Feedback is free for every studio. No plan-based
     * throttling at this layer.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Feedback", meta = (AutoCreateRefTerm = "OnDone"))
    void SubmitFeedback(
        const FString& FormId,
        const TArray<FPlayloopFeedbackResponse>& Responses,
        const FOnPlayloopFeedbackSubmittedBP& OnDone,
        const FString& RequestId = TEXT(""),
        const FString& OriginalSessionId = TEXT(""));

    /**
     * Build the SDK-default field set. Matches the dashboard's
     * "New form" starter (1-5 rating + "What worked?" short-text +
     * "What didn't?" long-text). Use when you'd rather not author a
     * form in the dashboard.
     */
    UFUNCTION(BlueprintPure, Category = "Playloop|Feedback")
    static TArray<FPlayloopFeedbackField> GetDefaultFeedbackFields();

    // -------------------------------------------------------------
    // Player Bug Reports (free; player-initiated)
    // -------------------------------------------------------------

    /**
     * Submit a player-initiated bug report. `Title` (1..200) and
     * `Description` (1..8000) are required; `Severity` defaults to Medium.
     * Playloop stores the report, surfaces it in the studio dashboard, and
     * routes it to the studio's connected issue tracker automatically when one
     * is configured.
     *
     * Environment context (build version / environment / platform) is
     * auto-filled from the client. The session is the launch's current one; an
     * idempotency key is generated per call so a transport retry dedupes to one
     * report. The optional `OnDone` delegate fires once with the parsed
     * `FPlayloopBugReportResult`.
     *
     * A disabled client (no API key) fails soft: `OnDone` fires with
     * `bOk = false` and NO network request, exactly like SubmitFeedback.
     *
     * Player Bug Reports are free for every studio.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|BugReports", meta = (AutoCreateRefTerm = "OnDone"))
    void SubmitBugReport(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FOnPlayloopBugReportSubmittedBP& OnDone);

    /**
     * SubmitBugReport with the optional fields exposed: your own `PlayerId`, an
     * explicit `SessionId` override, a caller-supplied `IdempotencyKey`, and
     * environment `Context` (auto-filled fields still win when left empty).
     * See `FPlayloopBugReportParams`.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|BugReports", meta = (AutoCreateRefTerm = "Params,OnDone"))
    void SubmitBugReportWithContext(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FPlayloopBugReportParams& Params,
        const FOnPlayloopBugReportSubmittedBP& OnDone);

    // -------------------------------------------------------------
    // Crashes
    // -------------------------------------------------------------

    /** Persist a managed (caught) exception. Drained on next session start. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Crashes")
    void ReportCrash(const FString& Message, const FString& StackTrace);

    // -------------------------------------------------------------
    // Singleton accessor (for editor "Send Test Event" + BP convenience)
    // -------------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Playloop")
    static UPlayloopClient* GetCurrent();

    /** Internal: called by the GameInstanceSubsystem. */
    static void SetCurrent(UPlayloopClient* Client);

    /**
     * Per-game settings resolved from the server's event-config (branding,
     * crash flags). Returns defaults before the fetch completes, so callers
     * (the feedback widget) always read a usable shape. The event-config
     * store also drives per-event sdkIgnore / linkToSummary in Track().
     */
    FPlayloopGameConfigBlock GetGameSettings() const;

    /** True once the event-config fetch has populated the cache. */
    bool IsEventConfigLoaded() const;

    /** Re-fetch the per-event config from the server (fire-and-forget). */
    UFUNCTION(BlueprintCallable, Category = "Playloop")
    void RefreshEventConfig();

    /** Bound config + read-only state accessors (for tests + editor). */
    const FPlayloopConfig& GetConfig() const { return Config; }
    FString GetSessionId() const { return CurrentSessionId; }
    FString GetDeviceId() const;
    int32 GetPendingEventCount() const;

    /** Test seam: swap out the HTTP client. */
    void SetHttpClientForTesting(TSharedPtr<FPlayloopHttpClient> InHttp);

    /** Test seam: skip auto-flush timer. */
    void SetTimersDisabledForTesting(bool bDisable) { bTimersDisabled = bDisable; }

private:
    void EmitSessionStart();
    void EmitSessionEnd();
    void EmitHeartbeat();
    void DrainCrashes();
    void StartTimers();
    void StopTimers();
    void OnFlushTimer();
    void OnHeartbeatTimer();
    TSharedPtr<FJsonObject> SnapshotState() const;
    /** Merge an event's props into the State accumulator (linkToSummary). */
    void MergeIntoState(const TSharedPtr<FJsonObject>& Data);
    /** Build + start the auto-instrument driver from Config (no-op if all arms off). */
    void StartAutoInstrument();
    /** Tear down the auto-instrument driver (delegates + ticker). */
    void StopAutoInstrument();
    FString ResolveTelemetryUrl() const;
    /** POST/DELETE claim endpoint (link + unlink share it). */
    FString ResolveClaimUrl() const;
    /** GET tester-status endpoint with the gameId + deviceId query string. */
    FString ResolveTesterStatusUrl(const FString& GameId, const FString& DeviceId) const;
    void PostTelemetry(TSharedPtr<FJsonObject> Envelope);

    FPlayloopConfig Config;
    FString CurrentSessionId;
    bool bStarted = false;
    bool bTimersDisabled = false;
    /** When true, telemetry sends are suppressed (editor / dev build, opt-out). */
    bool bSendSuppressed = false;
    mutable FCriticalSection StateMutex;
    TMap<FString, TSharedPtr<FJsonValue>> State;

    TSharedPtr<FPlayloopEventQueue> Queue;
    TSharedPtr<FPlayloopHttpClient> Http;
    TSharedPtr<FPlayloopEventConfigStore> EventConfig;
    TSharedPtr<FPlayloopGameResolver> Resolver;
    TSharedPtr<FPlayloopAutoInstrument> AutoInstrument;

    UPROPERTY()
    TObjectPtr<UPlayloopExperiments> ExperimentsObj;

    UPROPERTY()
    TObjectPtr<UPlayloopDiscord> DiscordObj;

    FTimerHandle FlushTimerHandle;
    FTimerHandle HeartbeatTimerHandle;
};

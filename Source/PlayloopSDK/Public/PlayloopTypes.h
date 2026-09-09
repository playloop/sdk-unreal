#pragma once

#include "CoreMinimal.h"
#include "PlayloopTypes.generated.h"

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopConfig
{
    GENERATED_BODY()

    /**
     * Playloop ingest key (Ingest scope) for this game. Each game has its own
     * ingest key. Copy it from that game's setup page on playloop.gg
     * (Connections -> Unreal). The key identifies the game, so every feature
     * (telemetry, experiments, Tester Keys) attributes to it automatically.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop")
    FString ApiKey;

    /** Environment tag stamped on every session (production / demo / dev / custom). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop")
    FString Environment = TEXT("dev");

    /** Build version surfaced in the dashboard. Usually pulled from Project Settings -> Project Version. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop")
    FString BuildVersion;

    /** Base URL for the Playloop API. Override for local development/testing only. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    FString ApiBaseUrl = TEXT("https://playloop.gg");

    /** Auto-flush cadence in ms. 0 disables auto-batch; call Flush manually. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    int32 FlushIntervalMs = 5000;

    /** Heartbeat cadence in ms. 0 disables heartbeats entirely. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    int32 HeartbeatIntervalMs = 60000;

    /** Install the crash trap (FCoreDelegates::OnHandleSystemError). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    bool bEnableCrashHandler = true;

    /** Max events to capture into a crash report's recentEvents tail. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    int32 CrashRecentEventsCount = 5;

    /** Max retries for transient HTTP failures. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    int32 MaxHttpRetries = 3;

    /**
     * Discord relay secret, sent as the x-playloop-secret header on Discord
     * ingest calls. Copy it from Connections > Discord on playloop.gg (it's
     * shown once; rotate it there any time). Only needed by processes that
     * call Discord()->IngestAsync(); telemetry and every other surface ignore
     * it. Can also be supplied per call via IngestAsync's RelaySecret
     * parameter. Like the management key, keep it server-side (a trusted
     * relay), never in a shipped player build. set it in code via
     * Configure(); it has no Project Settings entry on purpose.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    FString RelaySecret;

    /**
     * Eagerly fetch A/B experiment assignments at Start() instead of
     * lazily on the first VariantAsync() call. Turn this on when the very
     * first thing a player sees is gated on a variant.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    bool bPrefetchExperiments = false;

    /**
     * Send telemetry from the editor (PIE / -game) and non-shipping
     * (Debug / Development) builds. OFF by default: editor + dev-build runs
     * are suppressed so playtest data isn't polluted, and the SDK logs a
     * one-time warning explaining how to opt in. Turn this ON to exercise the
     * ingest pipeline locally; pair it with Environment = "dev" so the traffic
     * is easy to filter out. No effect in a Shipping player build (nothing is
     * suppressed there).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Advanced")
    bool bSendInEditor = false;

    // ── Auto-instrument: "useful by default" telemetry the SDK emits without
    // any per-game wiring. Each arm routes through Track(), so it respects
    // sdkIgnore + the editor-suppression gate. Names + payloads match the
    // Unity / Godot SDKs exactly. On by default (memory is opt-in).

    /** Emit `scene_changed {from,to}` when the active map/level changes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentScenes = true;

    /** Emit `focus_lost` / `focus_gained` when the app loses / regains focus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentFocus = true;

    /** Emit `idle_start` / `idle_end` around stretches with no user input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentIdle = true;

    /** Emit `fps_drop {medianFps,lowFps,windowSec}` when the rolling median FPS dips. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentFps = true;

    /** Emit `memory_pressure {totalMb,allocatedMb}` near the physical-RAM ceiling. Opt-in (noisy). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    bool bAutoInstrumentMemory = false;

    /** Idle threshold in seconds before `idle_start`. 0 → default (30 desktop / 60 mobile). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    float IdleThresholdSec = 0.f;

    /** Median FPS below this (over a ~3s window) fires `fps_drop`. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|AutoInstrument")
    float FpsDropThreshold = 30.f;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopLinkResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    bool bAlreadyLinked = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString Handle;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    int64 ClaimedAtMs = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopTesterStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    bool bLinked = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString Handle;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    int64 ClaimedAtMs = 0;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopUnlinkResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    bool bUnlinked = false;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop")
    FString ErrorMessage;
};

// ============================================================================
// Player Feedback (free for every studio). One form, one or more fields,
// one submission per call.
// ============================================================================

UENUM(BlueprintType)
enum class EPlayloopFeedbackFieldKind : uint8
{
    /** 1-5 star rating. UI renders as 5 buttons. */
    Rating1To5    UMETA(DisplayName = "Rating 1-5"),
    /** Single-line text input. */
    ShortText     UMETA(DisplayName = "Short text"),
    /** Multi-line text input. */
    LongText      UMETA(DisplayName = "Long text"),
    /** Binary choice. UI renders as Yes / No buttons. */
    YesNo         UMETA(DisplayName = "Yes / No"),
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopFeedbackField
{
    GENERATED_BODY()

    /** Stable id within the form ("q1", "q2", ...). Submissions reference fields by this id. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    FString Id;

    /** Player-facing label. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    FString Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    EPlayloopFeedbackFieldKind Kind = EPlayloopFeedbackFieldKind::ShortText;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    bool bRequired = false;

    /** Placeholder for text fields. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    FString Placeholder;

    /** Optional help text shown under the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|Feedback")
    FString HelpText;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopFeedbackResponse
{
    GENERATED_BODY()

    /** The field's id as defined on the form. */
    UPROPERTY(BlueprintReadWrite, Category = "Playloop|Feedback")
    FString FieldId;

    /** Stringified value. For ratings: the numeric value as a string ("5"). For yes/no: "yes" or "no". */
    UPROPERTY(BlueprintReadWrite, Category = "Playloop|Feedback")
    FString Value;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopFeedbackResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    bool bOk = false;

    /** Server-assigned id grouping the multi-field rows ("fs_..."). */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    FString SubmissionId;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    FString FormId;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    FString SessionId;

    /** Server-assigned id of every persisted response row ("fr_..."), in submission order. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    TArray<FString> ResponseIds;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    int32 Status = 0;

    /** Populated on failure (`unknown_field`, `no_responses`, etc.). Empty on success. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|Feedback")
    FString ErrorMessage;
};

// ============================================================================
// Player Bug Reports (free for every studio). A player-initiated bug report is
// ONE title + description (both required) with an optional severity. Playloop
// stores it, surfaces it in the studio dashboard, and routes it to the
// studio's connected issue tracker automatically when one is configured.
// ============================================================================

/** Severity accepted by SubmitBugReport. Defaults to Medium. */
UENUM(BlueprintType)
enum class EPlayloopBugSeverity : uint8
{
    Low      UMETA(DisplayName = "Low"),
    Medium   UMETA(DisplayName = "Medium"),
    High     UMETA(DisplayName = "High"),
    Critical UMETA(DisplayName = "Critical"),
};

/**
 * Optional environment context attached to a bug report. Every field is
 * optional. Whatever the client already knows (build version / environment /
 * platform) is auto-filled at submit time; any value set here wins field by
 * field. `Device` is a flat name -> value map (screen size, GPU, locale, ...)
 * your UI chooses to attach; values are sent as strings.
 */
USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopBugReportContext
{
    GENERATED_BODY()

    /** App build version. Auto-filled from the client's BuildVersion when left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString BuildVersion;

    /** Environment slug. Auto-filled from the client's Environment when left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString Environment;

    /** Platform label. Auto-filled from the running platform when left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString Platform;

    /** Free-form device details (name -> value), sent as strings. Optional. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    TMap<FString, FString> Device;
};

/**
 * Optional fields for a bug-report submission. Leave any of them empty/default
 * to fall back to the client-resolved value. Used by the SubmitBugReportWithContext
 * overload; the plain SubmitBugReport uses the defaults.
 */
USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopBugReportParams
{
    GENERATED_BODY()

    /** Your own id for the player, if you track one. Optional. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString PlayerId;

    /**
     * Session to attribute the report to. Empty means "use the launch's
     * current session" (resolved at submit time). Set it to override.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString SessionId;

    /**
     * Idempotency key. Empty means "generate a fresh one for this call" so a
     * transport retry dedupes to a single report. Set it to control the dedupe
     * key yourself.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FString IdempotencyKey;

    /** Optional environment context. Auto-filled fields win when left empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playloop|BugReports")
    FPlayloopBugReportContext Context;
};

USTRUCT(BlueprintType)
struct PLAYLOOPSDK_API FPlayloopBugReportResult
{
    GENERATED_BODY()

    /**
     * True on a stored report. False when the client is DISABLED (no API key:
     * the call fails soft rather than crashing), on a client-side validation
     * failure, or on a server reject. Branch on this exactly as the Player
     * Feedback surface does.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    bool bOk = false;

    /** Server-assigned id of the stored report ("br_..."). Empty on failure. */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    FString BugReportId;

    /**
     * Where the report ended up: `pending` (queued), `stored_only` (kept in
     * Playloop, no tracker connected), `routed` (delivered to the connected
     * issue tracker), `partial` (routed to some destinations), or `failed`
     * (routing failed; the report is still stored). Empty on a failure.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    FString RoutingStatus;

    /**
     * True when this call repeated an idempotency key the server had already
     * accepted, so the original report was returned unchanged.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    bool bIdempotent = false;

    /** HTTP status, when a request was made (0 for a client-side failure). */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    int32 Status = 0;

    /**
     * Populated on failure: `invalid_argument` / `disabled` (client-side), or
     * the server's `reason` / `http_error` / `parse_error`. Empty on success.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    FString ErrorCode;

    UPROPERTY(BlueprintReadOnly, Category = "Playloop|BugReports")
    FString ErrorMessage;
};

// The single-cast `…BP` / tester delegates below back the BlueprintCallable
// async methods (LinkTester / GetCurrentTester / UnlinkTester / SubmitFeedback):
// UHT only accepts single-cast dynamic delegates as BlueprintCallable function
// parameters, and each fires exactly once per call. The multicast
// FOnPlayloopFeedbackSubmitted stays for the widget's BlueprintAssignable event.

/** Multicast delegate fired when SubmitFeedback completes (widget event). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnPlayloopFeedbackSubmitted, const FPlayloopFeedbackResult&, Result);

/** Single-cast callback param for the BlueprintCallable SubmitFeedback. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopFeedbackSubmittedBP, const FPlayloopFeedbackResult&, Result);

/** Fired when LinkTester completes. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopTesterLinked, const FPlayloopLinkResult&, Result);

/** Fired when GetCurrentTester completes. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopTesterStatus, const FPlayloopTesterStatus&, Result);

/** Fired when UnlinkTester completes. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopTesterUnlinked, const FPlayloopUnlinkResult&, Result);

/** Multicast delegate fired when SubmitBugReport completes (widget event). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnPlayloopBugReportSubmitted, const FPlayloopBugReportResult&, Result);

/** Single-cast callback param for the BlueprintCallable SubmitBugReport. */
DECLARE_DYNAMIC_DELEGATE_OneParam(
    FOnPlayloopBugReportSubmittedBP, const FPlayloopBugReportResult&, Result);

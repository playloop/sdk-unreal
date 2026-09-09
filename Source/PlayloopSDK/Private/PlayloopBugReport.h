#pragma once

#include "CoreMinimal.h"
#include "PlayloopTypes.h"
#include "PlayloopHttpClient.h"

/**
 * Player Bug Reports: the pure logic layer behind UPlayloopClient::SubmitBugReport.
 *
 * Everything here is UObject-free, UWorld-free, and network-free so it can be
 * exercised by an automation spec with a synchronous fake sender (mirroring the
 * experiments FFetcher pattern). The client wires the real HTTP transport + its
 * live config into these functions; PlayloopBugReportSpec drives them directly.
 *
 * Wire endpoint: POST /api/telemetry/bug-report, JSON body (camelCase):
 *   { title, description, severity, idempotencyKey,
 *     sessionId?, playerId?, context? { buildVersion?, environment?, platform?, device? } }
 * Response 201 { ok, bugReportId, routingStatus }; a repeated idempotencyKey
 * comes back 200 { ..., idempotent: true }.
 */
namespace PlayloopBugReport
{
    // Field caps mirrored from the server contract + the sibling SDKs.
    static constexpr int32 kTitleMax        = 200;
    static constexpr int32 kDescriptionMax  = 8000;
    static constexpr int32 kBuildVersionMax = 120;
    static constexpr int32 kEnvironmentMax  = 60;
    static constexpr int32 kPlatformMax     = 120;

    /**
     * The transport seam. Given the resolved URL + serialized JSON body,
     * perform the POST and deliver the raw HTTP result to Done. The real path
     * wraps FPlayloopHttpClient::PostJson (so PostJson's retry/backoff still
     * applies); a spec injects a synchronous fake to assert the body and drive
     * the response mapping. Mirrors the experiments FFetcher lambda pattern.
     */
    using FSender = TFunction<void(const FString& Url, const FString& Body, TFunction<void(const FPlayloopHttpResult&)> Done)>;

    /**
     * Everything Submit needs from the live client, captured up front so the
     * orchestrator stays pure. `bEnabled` is false when the client has no API
     * key (disabled -> soft-fail with no network). The Auto* fields are the
     * client's own telemetry sources for context auto-fill.
     */
    struct FEnv
    {
        bool bEnabled = false;
        FString ApiBaseUrl;
        FString ResolvedSessionId;
        FString AutoEnvironment;
        FString AutoPlatform;
        FString AutoBuildVersion;
    };

    /** The wire string for a severity enum value. */
    PLAYLOOPSDK_API const TCHAR* SeverityToWire(EPlayloopBugSeverity Severity);

    /** Endpoint URL for a given base. */
    PLAYLOOPSDK_API FString ResolveUrl(const FString& ApiBaseUrl);

    /**
     * Resolve the idempotency key: a non-empty caller override wins, else a
     * fresh lowercase GUID. Built ONCE per Submit call (before the retrying
     * transport) so a redelivery re-sends the identical key and the server
     * dedupes to one report.
     */
    PLAYLOOPSDK_API FString ResolveIdempotencyKey(const FString& Supplied);

    /** Merge caller-supplied context over the client's auto-filled values (supplied wins), clamped to the field caps. */
    PLAYLOOPSDK_API FPlayloopBugReportContext ResolveContext(const FPlayloopBugReportContext& Supplied, const FEnv& Env);

    /**
     * Validate title/description and build the camelCase JSON body. Returns
     * true + fills OutBody on success; false + fills OutError (bOk=false,
     * ErrorCode="invalid_argument") on a client-side validation failure.
     */
    PLAYLOOPSDK_API bool BuildRequestBody(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FString& ResolvedSessionId,
        const FString& PlayerId,
        const FString& ResolvedIdempotencyKey,
        const FPlayloopBugReportContext& Context,
        FString& OutBody,
        FPlayloopBugReportResult& OutError);

    /**
     * Map a raw HTTP result to the typed result (ok / ids / routingStatus /
     * idempotent / error fields). A 2xx body with `ok:true` is a success; a
     * repeated idempotency key comes back with `idempotent:true`.
     */
    PLAYLOOPSDK_API FPlayloopBugReportResult MapHttpResult(const FPlayloopHttpResult& Result);

    /**
     * The full orchestrator. Validates args, resolves context + idempotency
     * key, soft-fails a disabled client (no network), otherwise sends via
     * Sender and maps the response. OnDone fires exactly once. Testable
     * end-to-end with a synchronous fake Sender.
     */
    PLAYLOOPSDK_API void Submit(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FPlayloopBugReportParams& Params,
        const FEnv& Env,
        const FSender& Sender,
        TFunction<void(const FPlayloopBugReportResult&)> OnDone);
}

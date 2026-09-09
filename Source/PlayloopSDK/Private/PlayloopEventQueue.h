#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"

/**
 * Pending-event buffer. Track() enqueues; Flush() drains, serializes,
 * and hands the JSON envelope to the HTTP client.
 *
 * Carries a small tail of the most recently tracked events so the
 * crash handler can include them in the persisted crash report
 * (recentEvents in the crash JSON).
 */
struct FPlayloopEvent
{
    FString Id;
    FString Name;
    int64 TimestampMs = 0;
    TSharedPtr<FJsonObject> Data;
};

class PLAYLOOPSDK_API FPlayloopEventQueue
{
public:
    void Enqueue(FPlayloopEvent Event);

    /** Drains all pending events; tail buffer is left intact. */
    TArray<FPlayloopEvent> DrainPending();

    /** Re-enqueue events at the front (used on flush failure). */
    void RestoreFront(TArray<FPlayloopEvent> Events);

    int32 PendingCount() const;

    /**
     * Upper bound on retained pending events. During a long offline stretch
     * each failed flush RestoreFront's its batch while new events keep
     * arriving, so the buffer is capped and the OLDEST events are dropped
     * once it's exceeded (bounded memory beats a perfect-but-unbounded log).
     * Matches the Unity SDK's default telemetry buffer cap.
     */
    static constexpr int32 MaxPending = 200;

    /** Returns a snapshot of the most recent N events for crash attribution. */
    TArray<FPlayloopEvent> SnapshotTail(int32 Count) const;

    /** Generates a 10-char Crockford base32 short id (same shape as the other SDKs). */
    static FString GenerateShortId();

    /** Serializes events into the canonical `/api/telemetry` POST body.
     *
     *  LinkedId + ConsentLevel carry cross-game identity.
     *  Session-create-only: pass empty strings on subsequent flushes
     *  during the same session; the builder skips them when empty.
     */
    static FString BuildEnvelopeJson(
        const TArray<FPlayloopEvent>& Events,
        const FString& SessionId,
        const FString& DeviceId,
        const FString& Environment,
        TSharedPtr<FJsonObject> SessionMetadata,
        TSharedPtr<FJsonObject> SessionEndState,
        bool bSessionEnded,
        const FString& LinkedId = FString(),
        const FString& ConsentLevel = FString(),
        const TMap<FString, FString>& ExperimentTags = TMap<FString, FString>()
    );

private:
    mutable FCriticalSection Mutex;
    TArray<FPlayloopEvent> Pending;
    TArray<FPlayloopEvent> Tail;
    static constexpr int32 MaxTail = 50;
};

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

struct FPlayloopHttpResult
{
    bool bOk = false;
    int32 Status = 0;
    FString Body;
    FString ErrorReason;
};

DECLARE_DELEGATE_OneParam(FOnPlayloopHttpDone, FPlayloopHttpResult);

class FPlayloopHttpClient
{
public:
    /**
     * POST a JSON body. Auto-retries on transient HTTP failures
     * (network error / 5xx / 408 / 429) with exponential backoff + jitter:
     * 500ms base, 5s cap, MaxRetries attempts. 4xx other than 408/429
     * is final.
     *
     * Callback fires on the game thread.
     */
    void PostJson(
        const FString& Url,
        const FString& ApiKey,
        const FString& Body,
        const FString& Environment,
        int32 MaxRetries,
        FOnPlayloopHttpDone OnDone
    );

    /**
     * PostJson variant that stamps extra request headers (name -> value) on
     * every attempt, including retries. Used by the Discord ingest surface
     * for the x-playloop-secret relay header.
     */
    void PostJson(
        const FString& Url,
        const FString& ApiKey,
        const FString& Body,
        const FString& Environment,
        int32 MaxRetries,
        const TMap<FString, FString>& ExtraHeaders,
        FOnPlayloopHttpDone OnDone
    );

    /**
     * GET a JSON resource. Same retry policy and game-thread callback
     * contract as PostJson; sends no request body.
     */
    void GetJson(
        const FString& Url,
        const FString& ApiKey,
        const FString& Environment,
        int32 MaxRetries,
        FOnPlayloopHttpDone OnDone
    );

    /**
     * DELETE a JSON resource, with a JSON body. Same retry policy and
     * game-thread callback contract as PostJson.
     */
    void DeleteJson(
        const FString& Url,
        const FString& ApiKey,
        const FString& Body,
        const FString& Environment,
        int32 MaxRetries,
        FOnPlayloopHttpDone OnDone
    );

private:
    void Attempt(
        const FString& Verb,
        const FString& Url,
        const FString& ApiKey,
        const FString& Body,
        const FString& Environment,
        const TMap<FString, FString>& ExtraHeaders,
        int32 Attempt,
        int32 MaxRetries,
        FOnPlayloopHttpDone OnDone
    );

    static bool IsTransient(int32 Status);
    static int32 BackoffMs(int32 Attempt);
};

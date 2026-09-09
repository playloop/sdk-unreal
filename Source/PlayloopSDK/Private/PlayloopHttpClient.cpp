#include "PlayloopHttpClient.h"
#include "PlayloopSDK.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"
// The retry backoff is scheduled on the thread-safe core ticker, which fires
// on the game thread after a real delay (unlike a next-tick timer, which
// ignores the delay). Works headless / without a UWorld or GameViewport.
#include "Containers/Ticker.h"

bool FPlayloopHttpClient::IsTransient(int32 Status)
{
    if (Status == 0) return true;
    if (Status == 408 || Status == 429) return true;
    return Status >= 500 && Status < 600;
}

int32 FPlayloopHttpClient::BackoffMs(int32 Attempt)
{
    const int32 Base = 500 * (1 << FMath::Min(Attempt, 4));
    const int32 Capped = FMath::Min(Base, 5000);
    const int32 Jitter = FMath::RandRange(0, Capped / 4);
    return Capped + Jitter;
}

void FPlayloopHttpClient::PostJson(
    const FString& Url,
    const FString& ApiKey,
    const FString& Body,
    const FString& Environment,
    int32 MaxRetries,
    FOnPlayloopHttpDone OnDone)
{
    Attempt(TEXT("POST"), Url, ApiKey, Body, Environment, TMap<FString, FString>(), 0, MaxRetries, OnDone);
}

void FPlayloopHttpClient::PostJson(
    const FString& Url,
    const FString& ApiKey,
    const FString& Body,
    const FString& Environment,
    int32 MaxRetries,
    const TMap<FString, FString>& ExtraHeaders,
    FOnPlayloopHttpDone OnDone)
{
    Attempt(TEXT("POST"), Url, ApiKey, Body, Environment, ExtraHeaders, 0, MaxRetries, OnDone);
}

void FPlayloopHttpClient::GetJson(
    const FString& Url,
    const FString& ApiKey,
    const FString& Environment,
    int32 MaxRetries,
    FOnPlayloopHttpDone OnDone)
{
    Attempt(TEXT("GET"), Url, ApiKey, FString(), Environment, TMap<FString, FString>(), 0, MaxRetries, OnDone);
}

void FPlayloopHttpClient::DeleteJson(
    const FString& Url,
    const FString& ApiKey,
    const FString& Body,
    const FString& Environment,
    int32 MaxRetries,
    FOnPlayloopHttpDone OnDone)
{
    Attempt(TEXT("DELETE"), Url, ApiKey, Body, Environment, TMap<FString, FString>(), 0, MaxRetries, OnDone);
}

void FPlayloopHttpClient::Attempt(
    const FString& Verb,
    const FString& Url,
    const FString& ApiKey,
    const FString& Body,
    const FString& Environment,
    const TMap<FString, FString>& ExtraHeaders,
    int32 AttemptIndex,
    int32 MaxRetries,
    FOnPlayloopHttpDone OnDone)
{
    const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(Verb);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    if (!ApiKey.IsEmpty())
    {
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
    }
    if (!Environment.IsEmpty())
    {
        Request->SetHeader(TEXT("X-Playloop-Environment"), Environment);
    }
    // Lets the telemetry route attribute the session source as
    // 'unreal-telemetry' so the dashboard's per-engine integration card
    // lights up. Missing header falls back to 'unity-telemetry' on the
    // server for back-compat with pre-header SDK builds.
    Request->SetHeader(TEXT("X-Playloop-SDK"), TEXT("unreal"));
    for (const TPair<FString, FString>& Header : ExtraHeaders)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }
    if (!Body.IsEmpty())
    {
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        Request->SetContentAsString(Body);
    }

    Request->OnProcessRequestComplete().BindLambda(
        [Verb, Url, ApiKey, Body, Environment, ExtraHeaders, AttemptIndex, MaxRetries, OnDone]
        (FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bSucceeded)
        {
            const int32 Status = InResponse.IsValid() ? InResponse->GetResponseCode() : 0;
            const bool bNetworkOk = bSucceeded && InResponse.IsValid();
            const bool bHttpOk = bNetworkOk && Status >= 200 && Status < 300;

            if (bHttpOk)
            {
                FPlayloopHttpResult Result;
                Result.bOk = true;
                Result.Status = Status;
                Result.Body = InResponse->GetContentAsString();
                AsyncTask(ENamedThreads::GameThread, [OnDone, Result]() { OnDone.ExecuteIfBound(Result); });
                return;
            }

            const bool bShouldRetry =
                AttemptIndex + 1 < MaxRetries &&
                IsTransient(bNetworkOk ? Status : 0);

            if (!bShouldRetry)
            {
                FPlayloopHttpResult Result;
                Result.bOk = false;
                Result.Status = Status;
                Result.Body = InResponse.IsValid() ? InResponse->GetContentAsString() : FString();
                Result.ErrorReason = bNetworkOk ? FString::Printf(TEXT("HTTP %d"), Status) : TEXT("network");
                AsyncTask(ENamedThreads::GameThread, [OnDone, Result]() { OnDone.ExecuteIfBound(Result); });
                return;
            }

            const int32 SleepMs = BackoffMs(AttemptIndex);
            UE_LOG(LogPlayloop, Verbose,
                TEXT("Playloop HTTP attempt %d failed (status=%d), retrying in %dms"),
                AttemptIndex + 1, Status, SleepMs);

            const float SleepSec = SleepMs / 1000.0f;
            // Fire the retry AFTER the backoff elapses. The core ticker runs on
            // the game thread (the callback contract) and honors the delay; the
            // previous SetTimerForNextTick / AsyncTask paths dropped SleepSec on
            // the floor and retried within a frame or two, defeating the backoff.
            FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateLambda(
                    [Verb, Url, ApiKey, Body, Environment, ExtraHeaders, AttemptIndex, MaxRetries, OnDone](float /*Delta*/)
                    {
                        FPlayloopHttpClient Inst;
                        Inst.Attempt(Verb, Url, ApiKey, Body, Environment, ExtraHeaders, AttemptIndex + 1, MaxRetries, OnDone);
                        return false; // one-shot; unregister after firing
                    }),
                SleepSec);
        });

    Request->ProcessRequest();
}

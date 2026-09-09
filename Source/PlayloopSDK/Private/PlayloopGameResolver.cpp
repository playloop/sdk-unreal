#include "PlayloopGameResolver.h"
#include "PlayloopHttpClient.h"
#include "PlayloopSDK.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FPlayloopGameResolver::FPlayloopGameResolver(
    TSharedPtr<FPlayloopHttpClient> InHttp,
    const FString& InApiKey,
    const FString& InApiBaseUrl,
    const FString& InEnvironment,
    int32 InMaxRetries)
    : Http(InHttp)
    , ApiKey(InApiKey)
    , ApiBaseUrl(InApiBaseUrl)
    , Environment(InEnvironment)
    , MaxRetries(InMaxRetries > 0 ? InMaxRetries : 3)
{
}

void FPlayloopGameResolver::ResolveAsync(FOnPlayloopGameResolved OnDone)
{
    // Cached attempt: answer synchronously.
    if (bResolved)
    {
        OnDone.ExecuteIfBound(bResolvedOk, Result);
        return;
    }

    if (OnDone.IsBound())
    {
        Pending.Add(OnDone);
    }

    if (bInFlight)
    {
        return;
    }
    bInFlight = true;
    DoResolve();
}

void FPlayloopGameResolver::Prefetch()
{
    // Best-effort warm-up. An unbound delegate still kicks off the fetch.
    ResolveAsync(FOnPlayloopGameResolved());
}

void FPlayloopGameResolver::DoResolve()
{
    TWeakPtr<FPlayloopGameResolver, ESPMode::ThreadSafe> WeakSelf = AsShared();
    auto Complete = FResolveComplete::CreateLambda(
        [WeakSelf](const FPlayloopResolvedGame& Game)
        {
            TSharedPtr<FPlayloopGameResolver, ESPMode::ThreadSafe> Self = WeakSelf.Pin();
            if (!Self.IsValid())
            {
                return;
            }
            const bool bOk = !Game.GameId.IsEmpty() && !Game.Slug.IsEmpty();
            Self->Result = bOk ? Game : FPlayloopResolvedGame();
            Self->bResolvedOk = bOk;
            Self->bResolved = true;
            Self->bInFlight = false;
            Self->FlushPending(bOk, Self->Result);
        });

    if (Resolver)
    {
        Resolver(Complete);
        return;
    }
    ResolveViaHttp(Complete);
}

void FPlayloopGameResolver::FlushPending(bool bOk, const FPlayloopResolvedGame& Game)
{
    TArray<FOnPlayloopGameResolved> Callbacks = MoveTemp(Pending);
    Pending.Reset();
    for (FOnPlayloopGameResolved& Cb : Callbacks)
    {
        Cb.ExecuteIfBound(bOk, Game);
    }
}

void FPlayloopGameResolver::ResolveViaHttp(FResolveComplete Complete)
{
    if (!Http.IsValid() || ApiKey.IsEmpty())
    {
        Complete.ExecuteIfBound(FPlayloopResolvedGame());
        return;
    }

    const FString Url = FString::Printf(TEXT("%s/api/telemetry/resolve"), *ApiBaseUrl);
    Http->GetJson(Url, ApiKey, Environment, MaxRetries,
        FOnPlayloopHttpDone::CreateLambda([Complete](FPlayloopHttpResult Result)
        {
            if (!Result.bOk || Result.Body.IsEmpty())
            {
                Complete.ExecuteIfBound(FPlayloopResolvedGame());
                return;
            }
            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
            if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
            {
                Complete.ExecuteIfBound(FPlayloopResolvedGame());
                return;
            }
            FPlayloopResolvedGame Game;
            Parsed->TryGetStringField(TEXT("gameId"), Game.GameId);
            Parsed->TryGetStringField(TEXT("slug"), Game.Slug);
            Parsed->TryGetStringField(TEXT("name"), Game.Name);
            Complete.ExecuteIfBound(Game);
        }));
}

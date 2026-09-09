#include "PlayloopEventConfigStore.h"
#include "PlayloopGameResolver.h"
#include "PlayloopHttpClient.h"
#include "PlayloopSDK.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FPlayloopEventConfigStore::FPlayloopEventConfigStore(
    TSharedPtr<FPlayloopHttpClient> InHttp,
    TSharedPtr<FPlayloopGameResolver> InResolver,
    const FString& InApiKey,
    const FString& InApiBaseUrl,
    const FString& InEnvironment,
    int32 InMaxRetries)
    : Http(InHttp)
    , Resolver(InResolver)
    , ApiKey(InApiKey)
    , ApiBaseUrl(InApiBaseUrl)
    , Environment(InEnvironment)
    , MaxRetries(InMaxRetries > 0 ? InMaxRetries : 3)
{
}

bool FPlayloopEventConfigStore::Lookup(const FString& EventName, FPlayloopEventConfigEntry& OutEntry) const
{
    if (EventName.IsEmpty())
    {
        return false;
    }
    FScopeLock Guard(&Mutex);
    if (const FPlayloopEventConfigEntry* Found = Entries.Find(EventName))
    {
        OutEntry = *Found;
        return true;
    }
    return false;
}

FPlayloopGameConfigBlock FPlayloopEventConfigStore::GameSettings() const
{
    FScopeLock Guard(&Mutex);
    return Game;
}

bool FPlayloopEventConfigStore::IsLoaded() const
{
    FScopeLock Guard(&Mutex);
    return bFetched;
}

FString FPlayloopEventConfigStore::ResolveUrl(const FString& Slug) const
{
    return FString::Printf(TEXT("%s/api/v1/games/%s/event-config"),
        *ApiBaseUrl,
        *FGenericPlatformHttp::UrlEncode(Slug));
}

void FPlayloopEventConfigStore::Refresh()
{
    if (!Http.IsValid() || !Resolver.IsValid())
    {
        return;
    }
    {
        // Single-flight guard. The HTTP callback runs on the game thread, so a
        // plain bool under the lock is enough. A second Refresh() while the
        // first is in flight is dropped (the in-flight one publishes the cache).
        FScopeLock Guard(&Mutex);
        if (bRefreshing)
        {
            return;
        }
        bRefreshing = true;
    }

    // Resolve the game slug from the ingest key, then fetch. Weak self so a
    // resolve/fetch that outlives the owning client safely no-ops.
    TWeakPtr<FPlayloopEventConfigStore, ESPMode::ThreadSafe> WeakSelf = AsShared();
    Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
        [WeakSelf](bool bOk, const FPlayloopResolvedGame& Game)
        {
            TSharedPtr<FPlayloopEventConfigStore, ESPMode::ThreadSafe> Self = WeakSelf.Pin();
            if (!Self.IsValid())
            {
                return;
            }
            if (!bOk || Game.Slug.IsEmpty())
            {
                // Key couldn't be resolved. Stay inert, clear the guard.
                FScopeLock Guard(&Self->Mutex);
                Self->bRefreshing = false;
                return;
            }
            Self->FetchWithSlug(Game.Slug);
        }));
}

void FPlayloopEventConfigStore::FetchWithSlug(const FString& Slug)
{
    TWeakPtr<FPlayloopEventConfigStore, ESPMode::ThreadSafe> WeakSelf = AsShared();

    Http->GetJson(ResolveUrl(Slug), ApiKey, Environment, MaxRetries,
        FOnPlayloopHttpDone::CreateLambda([WeakSelf](FPlayloopHttpResult Result)
        {
            TSharedPtr<FPlayloopEventConfigStore, ESPMode::ThreadSafe> Self = WeakSelf.Pin();
            if (!Self.IsValid())
            {
                return;
            }
            if (!Result.bOk || Result.Body.IsEmpty())
            {
                FScopeLock Guard(&Self->Mutex);
                Self->bRefreshing = false;
                return;
            }
            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
            if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
            {
                FScopeLock Guard(&Self->Mutex);
                Self->bRefreshing = false;
                return;
            }

            TMap<FString, FPlayloopEventConfigEntry> NextEntries;
            const TArray<TSharedPtr<FJsonValue>>* EventsArray = nullptr;
            if (Parsed->TryGetArrayField(TEXT("events"), EventsArray) && EventsArray)
            {
                for (const TSharedPtr<FJsonValue>& Value : *EventsArray)
                {
                    const TSharedPtr<FJsonObject>* Row = nullptr;
                    if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row || !(*Row).IsValid())
                    {
                        continue;
                    }
                    FString Name;
                    if (!(*Row)->TryGetStringField(TEXT("eventName"), Name) || Name.IsEmpty())
                    {
                        continue;
                    }
                    FPlayloopEventConfigEntry Entry;
                    Entry.EventName = Name;
                    (*Row)->TryGetBoolField(TEXT("sdkIgnore"), Entry.bSdkIgnore);
                    (*Row)->TryGetBoolField(TEXT("linkToSummary"), Entry.bLinkToSummary);
                    (*Row)->TryGetBoolField(TEXT("hideFromAi"), Entry.bHideFromAi);
                    (*Row)->TryGetStringField(TEXT("category"), Entry.Category);
                    NextEntries.Emplace(Name, Entry);
                }
            }

            // Per-game settings block. Apply defaults + clamp defensively so a
            // misbehaving deploy can't surprise the SDK with out-of-range values.
            FPlayloopGameConfigBlock NextGame;
            const TSharedPtr<FJsonObject>* GameObj = nullptr;
            if (Parsed->TryGetObjectField(TEXT("game"), GameObj) && GameObj && (*GameObj).IsValid())
            {
                (*GameObj)->TryGetStringField(TEXT("summaryEventName"), NextGame.SnapshotEventName);
                (*GameObj)->TryGetBoolField(TEXT("crashesEnabled"), NextGame.bCrashesEnabled);
                (*GameObj)->TryGetBoolField(TEXT("showBranding"), NextGame.bShowBranding);
                double Count = 5.0;
                if ((*GameObj)->TryGetNumberField(TEXT("crashRecentEventsCount"), Count))
                {
                    NextGame.CrashRecentEventsCount = FMath::Clamp(static_cast<int32>(Count), 0, 50);
                }
                double Rate = 1.0;
                if ((*GameObj)->TryGetNumberField(TEXT("crashSampleRate"), Rate))
                {
                    NextGame.CrashSampleRate = FMath::Clamp(Rate, 0.0, 1.0);
                }
            }

            FScopeLock Guard(&Self->Mutex);
            Self->Entries = MoveTemp(NextEntries);
            Self->Game = NextGame;
            Self->bFetched = true;
            Self->bRefreshing = false;
            UE_LOG(LogPlayloop, Verbose, TEXT("Event-config loaded (%d rows, showBranding=%s)."),
                Self->Entries.Num(), Self->Game.bShowBranding ? TEXT("true") : TEXT("false"));
        }));
}

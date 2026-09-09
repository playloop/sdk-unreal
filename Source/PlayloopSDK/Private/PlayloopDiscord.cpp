#include "PlayloopDiscord.h"
#include "PlayloopHttpClient.h"
#include "PlayloopSDK.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UPlayloopDiscord::Initialize(const FPlayloopConfig& InConfig, TSharedPtr<FPlayloopHttpClient> InHttp)
{
    Config = InConfig;
    Http = InHttp;

    TWeakObjectPtr<UPlayloopDiscord> WeakThis(this);
    Fetcher = [WeakThis](const FString& Body, const FString& RelaySecret, FFetchComplete Complete)
    {
        UPlayloopDiscord* Self = WeakThis.Get();
        if (!Self)
        {
            Complete.ExecuteIfBound(false, 0, FString());
            return;
        }
        Self->PostViaHttp(Body, RelaySecret, Complete);
    };
}

void UPlayloopDiscord::IngestAsync(
    const FString& ChannelId,
    const FString& Game,
    const FString& ThreadId,
    const FString& RelaySecret,
    FOnPlayloopDiscordIngested OnDone)
{
    // Validate up front: mirror the throw-before-HTTP behavior of the other
    // SDKs, expressed as a synchronous failure result (Unreal can't throw
    // across a delegate boundary). The message names the offending field.
    if (ChannelId.IsEmpty())
    {
        FPlayloopDiscordIngestResult Failure;
        Failure.ErrorCode = TEXT("invalid_argument");
        Failure.ErrorMessage = TEXT("channelId is required.");
        OnDone.ExecuteIfBound(Failure);
        return;
    }
    if (Game.IsEmpty())
    {
        FPlayloopDiscordIngestResult Failure;
        Failure.ErrorCode = TEXT("invalid_argument");
        Failure.ErrorMessage = TEXT("game is required.");
        OnDone.ExecuteIfBound(Failure);
        return;
    }

    // Build the snake_case wire body. thread_id is omitted entirely when
    // empty (not sent as null), same as the reference SDKs.
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("channel_id"), ChannelId);
    Body->SetStringField(TEXT("game"), Game);
    if (!ThreadId.IsEmpty())
    {
        Body->SetStringField(TEXT("thread_id"), ThreadId);
    }

    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    if (!Fetcher)
    {
        // Initialize() never ran. There's no transport to call.
        FPlayloopDiscordIngestResult Failure;
        Failure.ErrorCode = TEXT("not_initialized");
        Failure.ErrorMessage = TEXT("Discord API is not initialized. Configure() the client first.");
        OnDone.ExecuteIfBound(Failure);
        return;
    }

    // Per-call secret wins over the client-level config; empty means "none
    // configured" and the header is omitted (the SDK never errors on it).
    const FString ResolvedRelaySecret = !RelaySecret.IsEmpty() ? RelaySecret : Config.RelaySecret;

    Fetcher(Out, ResolvedRelaySecret, FFetchComplete::CreateLambda(
        [OnDone](bool bOk, int32 Status, const FString& ResponseBody)
        {
            FPlayloopDiscordIngestResult Result;
            Result.Status = Status;

            if (!bOk)
            {
                Result.bOk = false;
                Result.ErrorCode = TEXT("http_error");
                Result.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Status);
                UE_LOG(LogPlayloop, Log, TEXT("Discord ingest failed (status=%d)."), Status);
                OnDone.ExecuteIfBound(Result);
                return;
            }

            // A 2xx with an empty/parseless body is still a success. The
            // append case (200) can come back without counts. Default both
            // counts to 0 in that case.
            if (!ResponseBody.IsEmpty())
            {
                TSharedPtr<FJsonObject> Parsed;
                const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
                if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
                {
                    double Num = 0.0;
                    if (Parsed->TryGetNumberField(TEXT("sessionsCreated"), Num))
                    {
                        Result.SessionsCreated = static_cast<int32>(Num);
                    }
                    if (Parsed->TryGetNumberField(TEXT("messagesIngested"), Num))
                    {
                        Result.MessagesIngested = static_cast<int32>(Num);
                    }
                }
                else
                {
                    Result.bOk = false;
                    Result.ErrorCode = TEXT("parse_error");
                    Result.ErrorMessage = TEXT("Server response was not valid JSON.");
                    OnDone.ExecuteIfBound(Result);
                    return;
                }
            }

            Result.bOk = true;
            UE_LOG(LogPlayloop, Log, TEXT("Discord ingest ok (sessionsCreated=%d, messagesIngested=%d)."),
                Result.SessionsCreated, Result.MessagesIngested);
            OnDone.ExecuteIfBound(Result);
        }));
}

void UPlayloopDiscord::IngestAsync(
    const FString& ChannelId,
    const FString& Game,
    const FString& ThreadId,
    const FString& RelaySecret,
    const FOnPlayloopDiscordIngestedBP& OnDone)
{
    FOnPlayloopDiscordIngestedBP Bound = OnDone;
    IngestAsync(ChannelId, Game, ThreadId, RelaySecret, FOnPlayloopDiscordIngested::CreateLambda(
        [Bound](const FPlayloopDiscordIngestResult& Result)
        {
            Bound.Execute(Result);
        }));
}

void UPlayloopDiscord::PostViaHttp(const FString& Body, const FString& RelaySecret, FFetchComplete Complete)
{
    if (!Http.IsValid())
    {
        Complete.ExecuteIfBound(false, 0, FString());
        return;
    }

    // The relay secret rides only this endpoint, and only when one is
    // configured. no header (and no SDK-level error) otherwise.
    TMap<FString, FString> ExtraHeaders;
    if (!RelaySecret.IsEmpty())
    {
        ExtraHeaders.Add(TEXT("x-playloop-secret"), RelaySecret);
    }

    const FString Url = FString::Printf(TEXT("%s/api/webhooks/discord"), *Config.ApiBaseUrl);
    Http->PostJson(Url, Config.ApiKey, Body, Config.Environment, Config.MaxHttpRetries, ExtraHeaders,
        FOnPlayloopHttpDone::CreateLambda([Complete](FPlayloopHttpResult Result)
        {
            Complete.ExecuteIfBound(Result.bOk, Result.Status, Result.Body);
        }));
}

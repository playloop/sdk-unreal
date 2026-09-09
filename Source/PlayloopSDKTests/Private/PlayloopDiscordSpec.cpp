#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopDiscord.h"

// Mirrors the discord ingest specs in the TS / Python / Unity / Godot SDKs,
// using an injected, synchronous fetcher so each spec is deterministic and
// needs no real network:
//   1. POSTs a snake_case body to the ingest endpoint and parses the result.
//   2. Includes thread_id when a threadId is provided.
//   3. Omits thread_id entirely when none is provided.
//   4. Empty channelId fails up front, before any HTTP.
//   5. Empty game fails up front, before any HTTP.
//   6. The client-level RelaySecret rides the call (x-playloop-secret).
//   7. A per-call RelaySecret overrides the client-level one.
//   8. No secret configured -> no secret resolved (header omitted).

BEGIN_DEFINE_SPEC(FPlayloopDiscordSpec, "Playloop.Discord",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)

    // Builds a fetcher that completes synchronously: records the request body,
    // resolved relay secret + invocation count, then answers with a canned
    // (bOk, status, body).
    static UPlayloopDiscord::FFetcher MakeFetcher(
        TSharedRef<int32> Counter,
        TSharedRef<FString> CapturedBody,
        TSharedRef<FString> CapturedSecret,
        TSharedRef<bool> bOk,
        TSharedRef<int32> Status,
        TSharedRef<FString> ResponseBody)
    {
        return [Counter, CapturedBody, CapturedSecret, bOk, Status, ResponseBody](
            const FString& Body, const FString& RelaySecret, UPlayloopDiscord::FFetchComplete Complete)
        {
            (*Counter)++;
            *CapturedBody = Body;
            *CapturedSecret = RelaySecret;
            Complete.ExecuteIfBound(*bOk, *Status, *ResponseBody);
        };
    }

END_DEFINE_SPEC(FPlayloopDiscordSpec)

void FPlayloopDiscordSpec::Define()
{
    Describe("Ingest", [this]()
    {
        It("POSTs a snake_case body and parses the result", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":1,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestEqual("one fetch was made", *Count, 1);
            TestTrue("body carries snake_case channel_id", Body->Contains(TEXT("\"channel_id\"")));
            TestTrue("body carries channel value", Body->Contains(TEXT("123456")));
            TestTrue("body carries game", Body->Contains(TEXT("\"game\"")));
            TestTrue("body carries game value", Body->Contains(TEXT("my-game")));
            TestTrue("result ok", Result.bOk);
            TestEqual("sessionsCreated parsed", Result.SessionsCreated, 1);
            TestEqual("messagesIngested parsed", Result.MessagesIngested, 0);
            TestEqual("status echoed", Result.Status, 201);
        });

        It("includes thread_id when provided", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(200);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":0,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), TEXT("thread-789"), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestTrue("body carries thread_id key", Body->Contains(TEXT("\"thread_id\"")));
            TestTrue("body carries thread value", Body->Contains(TEXT("thread-789")));
            TestTrue("result ok (append case)", Result.bOk);
            TestEqual("append reports no new session", Result.SessionsCreated, 0);
        });

        It("omits thread_id when not provided", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":1,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [](const FPlayloopDiscordIngestResult&) {}));

            TestFalse("body has no thread_id key", Body->Contains(TEXT("thread_id")));
        });
    });

    Describe("Validation", [this]()
    {
        It("requires channelId and never hits the network", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(TEXT("{}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(FString(), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestFalse("result not ok", Result.bOk);
            TestEqual("error code is invalid_argument", Result.ErrorCode, FString(TEXT("invalid_argument")));
            TestTrue("message names channelId", Result.ErrorMessage.Contains(TEXT("channelId")));
            TestEqual("no fetch attempted", *Count, 0);
        });

        It("requires game and never hits the network", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(TEXT("{}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), FString(), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestFalse("result not ok", Result.bOk);
            TestEqual("error code is invalid_argument", Result.ErrorCode, FString(TEXT("invalid_argument")));
            TestTrue("message names game", Result.ErrorMessage.Contains(TEXT("game")));
            TestEqual("no fetch attempted", *Count, 0);
        });
    });

    Describe("Relay secret", [this]()
    {
        It("resolves the client-level RelaySecret for the call", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            FPlayloopConfig Config;
            Config.RelaySecret = TEXT("client-secret");
            Discord->Initialize(Config, nullptr);

            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":1,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestEqual("one fetch was made", *Count, 1);
            TestEqual("client-level secret resolved", *Secret, FString(TEXT("client-secret")));
            TestTrue("result ok", Result.bOk);
        });

        It("per-call RelaySecret overrides the client-level one", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            FPlayloopConfig Config;
            Config.RelaySecret = TEXT("client-secret");
            Discord->Initialize(Config, nullptr);

            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(200);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":0,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), TEXT("call-secret"),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [](const FPlayloopDiscordIngestResult&) {}));

            TestEqual("per-call secret wins", *Secret, FString(TEXT("call-secret")));
        });

        It("resolves no secret when none is configured", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>(TEXT("sentinel"));
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<int32> Status = MakeShared<int32>(201);
            TSharedRef<FString> Resp = MakeShared<FString>(
                TEXT("{\"sessionsCreated\":1,\"messagesIngested\":0}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestEqual("one fetch was made", *Count, 1);
            TestEqual("no secret resolved (header omitted)", *Secret, FString());
            TestTrue("result ok (no SDK-level error for a missing secret)", Result.bOk);
        });
    });

    Describe("Errors", [this]()
    {
        It("reports an http_error on a non-2xx response", [this]()
        {
            UPlayloopDiscord* Discord = NewObject<UPlayloopDiscord>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FString> Secret = MakeShared<FString>();
            TSharedRef<bool> Ok = MakeShared<bool>(false);
            TSharedRef<int32> Status = MakeShared<int32>(401);
            TSharedRef<FString> Resp = MakeShared<FString>(TEXT("{\"error\":\"unauthorized\"}"));
            Discord->SetFetcherForTesting(MakeFetcher(Count, Body, Secret, Ok, Status, Resp));

            FPlayloopDiscordIngestResult Result;
            Discord->IngestAsync(TEXT("123456"), TEXT("my-game"), FString(), FString(),
                FOnPlayloopDiscordIngested::CreateLambda(
                    [&Result](const FPlayloopDiscordIngestResult& R) { Result = R; }));

            TestFalse("result not ok", Result.bOk);
            TestEqual("error code is http_error", Result.ErrorCode, FString(TEXT("http_error")));
            TestEqual("status echoed", Result.Status, 401);
        });
    });
}

#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopBugReport.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Covers the Player Bug Report logic layer (PlayloopBugReport::Submit and its
// helpers) with an injected, synchronous fake sender so each spec is
// deterministic and needs no real network, UObject, or UWorld. mirroring the
// experiments FFetcher pattern. This is the first spec for this surface, so it
// sets the precedent: logic-layer only.
//
//   1. Request body: camelCase keys, auto-filled context, optional fields.
//   2. Idempotency: a per-call GUID that's stable within a call, unique across.
//   3. Validation: empty / over-long title + description are rejected.
//   4. Disabled soft-fail: no API key resolves to bOk=false, no network.
//   5. Response mapping: success, idempotent repeat, server reject, HTTP error.

BEGIN_DEFINE_SPEC(FPlayloopBugReportSpec, "Playloop.BugReport",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)

    // A sender that completes synchronously: records how many times it was
    // called, captures the URL + body, and replies with a canned HTTP result.
    static PlayloopBugReport::FSender MakeSender(
        TSharedRef<int32> CallCount,
        TSharedRef<FString> OutUrl,
        TSharedRef<FString> OutBody,
        TSharedRef<FPlayloopHttpResult> Response)
    {
        return [CallCount, OutUrl, OutBody, Response](
            const FString& Url, const FString& Body, TFunction<void(const FPlayloopHttpResult&)> Done)
        {
            (*CallCount)++;
            *OutUrl = Url;
            *OutBody = Body;
            Done(*Response);
        };
    }

    // A default enabled environment: API key present, a resolved session, and
    // the client's auto-fill context sources populated.
    static PlayloopBugReport::FEnv EnabledEnv()
    {
        PlayloopBugReport::FEnv Env;
        Env.bEnabled = true;
        Env.ApiBaseUrl = TEXT("https://playloop.gg");
        Env.ResolvedSessionId = TEXT("sess_123");
        Env.AutoEnvironment = TEXT("production");
        Env.AutoPlatform = TEXT("Unreal/Windows");
        Env.AutoBuildVersion = TEXT("1.2.3");
        return Env;
    }

    static TSharedPtr<FJsonObject> ParseJson(const FString& Body)
    {
        TSharedPtr<FJsonObject> Obj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
        FJsonSerializer::Deserialize(Reader, Obj);
        return Obj;
    }

    static TSharedRef<FPlayloopHttpResult> HttpOk(int32 Status, const FString& Body)
    {
        TSharedRef<FPlayloopHttpResult> R = MakeShared<FPlayloopHttpResult>();
        R->bOk = true;
        R->Status = Status;
        R->Body = Body;
        return R;
    }

END_DEFINE_SPEC(FPlayloopBugReportSpec)

void FPlayloopBugReportSpec::Define()
{
    Describe("Severity wire mapping", [this]()
    {
        It("maps each enum value to its lowercase slug", [this]()
        {
            TestEqual("low", FString(PlayloopBugReport::SeverityToWire(EPlayloopBugSeverity::Low)), FString(TEXT("low")));
            TestEqual("medium", FString(PlayloopBugReport::SeverityToWire(EPlayloopBugSeverity::Medium)), FString(TEXT("medium")));
            TestEqual("high", FString(PlayloopBugReport::SeverityToWire(EPlayloopBugSeverity::High)), FString(TEXT("high")));
            TestEqual("critical", FString(PlayloopBugReport::SeverityToWire(EPlayloopBugSeverity::Critical)), FString(TEXT("critical")));
        });
    });

    Describe("Request body", [this]()
    {
        It("builds a camelCase body with auto-filled context", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true,\"bugReportId\":\"br_1\",\"routingStatus\":\"pending\"}"));

            PlayloopBugReport::Submit(
                TEXT("Crash on boot"), TEXT("It crashes when I press start."),
                EPlayloopBugSeverity::High, FPlayloopBugReportParams(), EnabledEnv(),
                MakeSender(Calls, Url, Body, Resp), [](const FPlayloopBugReportResult&) {});

            TestEqual("sender called once", *Calls, 1);
            TestEqual("posts to the bug-report endpoint", *Url, FString(TEXT("https://playloop.gg/api/telemetry/bug-report")));

            TSharedPtr<FJsonObject> Json = ParseJson(*Body);
            TestTrue("body is valid JSON", Json.IsValid());
            if (!Json.IsValid()) return;
            TestEqual("title", Json->GetStringField(TEXT("title")), FString(TEXT("Crash on boot")));
            TestEqual("description", Json->GetStringField(TEXT("description")), FString(TEXT("It crashes when I press start.")));
            TestEqual("severity", Json->GetStringField(TEXT("severity")), FString(TEXT("high")));
            TestTrue("idempotencyKey present", Json->HasField(TEXT("idempotencyKey")) && !Json->GetStringField(TEXT("idempotencyKey")).IsEmpty());
            TestEqual("sessionId auto-resolves", Json->GetStringField(TEXT("sessionId")), FString(TEXT("sess_123")));
            TestFalse("no playerId when not supplied", Json->HasField(TEXT("playerId")));

            const TSharedPtr<FJsonObject>* Ctx = nullptr;
            TestTrue("context present", Json->TryGetObjectField(TEXT("context"), Ctx) && Ctx != nullptr);
            if (Ctx && *Ctx)
            {
                TestEqual("context.buildVersion auto-filled", (*Ctx)->GetStringField(TEXT("buildVersion")), FString(TEXT("1.2.3")));
                TestEqual("context.environment auto-filled", (*Ctx)->GetStringField(TEXT("environment")), FString(TEXT("production")));
                TestEqual("context.platform auto-filled", (*Ctx)->GetStringField(TEXT("platform")), FString(TEXT("Unreal/Windows")));
            }
        });

        It("includes playerId + explicit session and honors a context override", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true,\"bugReportId\":\"br_2\",\"routingStatus\":\"routed\"}"));

            FPlayloopBugReportParams Params;
            Params.PlayerId = TEXT("player_42");
            Params.SessionId = TEXT("sess_override");
            Params.Context.Platform = TEXT("Unreal/Switch");
            Params.Context.Device.Add(TEXT("gpu"), TEXT("NX"));

            PlayloopBugReport::Submit(
                TEXT("UI glitch"), TEXT("The menu flickers."), EPlayloopBugSeverity::Low,
                Params, EnabledEnv(), MakeSender(Calls, Url, Body, Resp), [](const FPlayloopBugReportResult&) {});

            TSharedPtr<FJsonObject> Json = ParseJson(*Body);
            TestTrue("body is valid JSON", Json.IsValid());
            if (!Json.IsValid()) return;
            TestEqual("playerId", Json->GetStringField(TEXT("playerId")), FString(TEXT("player_42")));
            TestEqual("explicit sessionId wins", Json->GetStringField(TEXT("sessionId")), FString(TEXT("sess_override")));

            const TSharedPtr<FJsonObject>* Ctx = nullptr;
            Json->TryGetObjectField(TEXT("context"), Ctx);
            if (Ctx && *Ctx)
            {
                TestEqual("context.platform override wins", (*Ctx)->GetStringField(TEXT("platform")), FString(TEXT("Unreal/Switch")));
                const TSharedPtr<FJsonObject>* Device = nullptr;
                TestTrue("device present", (*Ctx)->TryGetObjectField(TEXT("device"), Device) && Device != nullptr);
                if (Device && *Device)
                {
                    TestEqual("device.gpu", (*Device)->GetStringField(TEXT("gpu")), FString(TEXT("NX")));
                }
            }
        });

        It("omits sessionId when neither explicit nor resolved", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true,\"bugReportId\":\"br_3\",\"routingStatus\":\"stored_only\"}"));

            PlayloopBugReport::FEnv Env = EnabledEnv();
            Env.ResolvedSessionId = FString();

            PlayloopBugReport::Submit(
                TEXT("No-session report"), TEXT("Filed before a session started."),
                EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(), Env,
                MakeSender(Calls, Url, Body, Resp), [](const FPlayloopBugReportResult&) {});

            TSharedPtr<FJsonObject> Json = ParseJson(*Body);
            TestTrue("body is valid JSON", Json.IsValid());
            if (!Json.IsValid()) return;
            TestFalse("no sessionId field", Json->HasField(TEXT("sessionId")));
        });
    });

    Describe("Idempotency key", [this]()
    {
        It("auto-generates a GUID-shaped key, unique per call", [this]()
        {
            auto KeyFor = [this]() -> FString
            {
                TSharedRef<int32> Calls = MakeShared<int32>(0);
                TSharedRef<FString> Url = MakeShared<FString>();
                TSharedRef<FString> Body = MakeShared<FString>();
                TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true,\"bugReportId\":\"br\",\"routingStatus\":\"pending\"}"));
                PlayloopBugReport::Submit(
                    TEXT("t"), TEXT("d"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                    EnabledEnv(), MakeSender(Calls, Url, Body, Resp), [](const FPlayloopBugReportResult&) {});
                TSharedPtr<FJsonObject> Json = ParseJson(*Body);
                return Json.IsValid() ? Json->GetStringField(TEXT("idempotencyKey")) : FString();
            };

            const FString KeyA = KeyFor();
            const FString KeyB = KeyFor();
            TestEqual("GUID length", KeyA.Len(), 36);
            TestTrue("GUID has hyphens", KeyA.Contains(TEXT("-")));
            TestTrue("keys are unique per call", KeyA != KeyB);
        });

        It("honors an explicit idempotency key", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true,\"bugReportId\":\"br\",\"routingStatus\":\"pending\"}"));

            FPlayloopBugReportParams Params;
            Params.IdempotencyKey = TEXT("my-fixed-key");

            PlayloopBugReport::Submit(
                TEXT("t"), TEXT("d"), EPlayloopBugSeverity::Medium, Params, EnabledEnv(),
                MakeSender(Calls, Url, Body, Resp), [](const FPlayloopBugReportResult&) {});

            TSharedPtr<FJsonObject> Json = ParseJson(*Body);
            TestTrue("body is valid JSON", Json.IsValid());
            if (!Json.IsValid()) return;
            TestEqual("explicit key is used verbatim", Json->GetStringField(TEXT("idempotencyKey")), FString(TEXT("my-fixed-key")));
        });
    });

    Describe("Validation", [this]()
    {
        auto ExpectReject = [this](const FString& Title, const FString& Description, const TCHAR* What)
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true}"));

            FPlayloopBugReportResult Result;
            bool bFired = false;
            PlayloopBugReport::Submit(
                Title, Description, EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                EnabledEnv(), MakeSender(Calls, Url, Body, Resp),
                [&Result, &bFired](const FPlayloopBugReportResult& R) { Result = R; bFired = true; });

            TestTrue(FString::Printf(TEXT("%s: callback fired"), What), bFired);
            TestFalse(FString::Printf(TEXT("%s: not ok"), What), Result.bOk);
            TestEqual(FString::Printf(TEXT("%s: invalid_argument"), What), Result.ErrorCode, FString(TEXT("invalid_argument")));
            TestEqual(FString::Printf(TEXT("%s: no network"), What), *Calls, 0);
        };

        It("rejects an empty title", [this, ExpectReject]()
        {
            ExpectReject(FString(), TEXT("desc"), TEXT("empty title"));
        });

        It("rejects an over-long title", [this, ExpectReject]()
        {
            ExpectReject(FString::ChrN(201, 'x'), TEXT("desc"), TEXT("long title"));
        });

        It("rejects an empty description", [this, ExpectReject]()
        {
            ExpectReject(TEXT("title"), FString(), TEXT("empty description"));
        });

        It("rejects an over-long description", [this, ExpectReject]()
        {
            ExpectReject(TEXT("title"), FString::ChrN(8001, 'x'), TEXT("long description"));
        });
    });

    Describe("Disabled soft-fail", [this]()
    {
        It("resolves bOk=false with no network when the client has no API key", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true}"));

            PlayloopBugReport::FEnv Env = EnabledEnv();
            Env.bEnabled = false;

            FPlayloopBugReportResult Result;
            bool bFired = false;
            PlayloopBugReport::Submit(
                TEXT("title"), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                Env, MakeSender(Calls, Url, Body, Resp),
                [&Result, &bFired](const FPlayloopBugReportResult& R) { Result = R; bFired = true; });

            TestTrue("callback fired", bFired);
            TestFalse("not ok", Result.bOk);
            TestEqual("error code", Result.ErrorCode, FString(TEXT("disabled")));
            TestEqual("no network", *Calls, 0);
        });

        It("still validates args before the disabled soft-fail", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201, TEXT("{\"ok\":true}"));

            PlayloopBugReport::FEnv Env = EnabledEnv();
            Env.bEnabled = false;

            FPlayloopBugReportResult Result;
            PlayloopBugReport::Submit(
                FString(), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                Env, MakeSender(Calls, Url, Body, Resp),
                [&Result](const FPlayloopBugReportResult& R) { Result = R; });

            TestEqual("validation error wins over disabled", Result.ErrorCode, FString(TEXT("invalid_argument")));
        });
    });

    Describe("Response mapping", [this]()
    {
        It("maps a stored report to a success result", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(201,
                TEXT("{\"ok\":true,\"bugReportId\":\"br_ok\",\"routingStatus\":\"routed\"}"));

            FPlayloopBugReportResult Result;
            PlayloopBugReport::Submit(
                TEXT("title"), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                EnabledEnv(), MakeSender(Calls, Url, Body, Resp),
                [&Result](const FPlayloopBugReportResult& R) { Result = R; });

            TestTrue("ok", Result.bOk);
            TestEqual("bugReportId", Result.BugReportId, FString(TEXT("br_ok")));
            TestEqual("routingStatus", Result.RoutingStatus, FString(TEXT("routed")));
            TestFalse("not idempotent", Result.bIdempotent);
            TestEqual("status", Result.Status, 201);
        });

        It("flags an idempotent repeat", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = HttpOk(200,
                TEXT("{\"ok\":true,\"bugReportId\":\"br_ok\",\"routingStatus\":\"routed\",\"idempotent\":true}"));

            FPlayloopBugReportResult Result;
            PlayloopBugReport::Submit(
                TEXT("title"), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                EnabledEnv(), MakeSender(Calls, Url, Body, Resp),
                [&Result](const FPlayloopBugReportResult& R) { Result = R; });

            TestTrue("ok", Result.bOk);
            TestTrue("idempotent", Result.bIdempotent);
            TestEqual("status", Result.Status, 200);
        });

        It("maps a server reject (4xx with a JSON reason body) to its reason", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            // A 429 arrives from the transport as bOk=false, but with the
            // server's JSON error envelope in the body.
            TSharedRef<FPlayloopHttpResult> Resp = MakeShared<FPlayloopHttpResult>();
            Resp->bOk = false;
            Resp->Status = 429;
            Resp->Body = TEXT("{\"ok\":false,\"reason\":\"rate_limited\",\"message\":\"Slow down.\"}");

            FPlayloopBugReportResult Result;
            PlayloopBugReport::Submit(
                TEXT("title"), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                EnabledEnv(), MakeSender(Calls, Url, Body, Resp),
                [&Result](const FPlayloopBugReportResult& R) { Result = R; });

            TestFalse("not ok", Result.bOk);
            TestEqual("error code from reason", Result.ErrorCode, FString(TEXT("rate_limited")));
            TestEqual("error message", Result.ErrorMessage, FString(TEXT("Slow down.")));
        });

        It("maps a transport failure to http_error", [this]()
        {
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            TSharedRef<FString> Url = MakeShared<FString>();
            TSharedRef<FString> Body = MakeShared<FString>();
            TSharedRef<FPlayloopHttpResult> Resp = MakeShared<FPlayloopHttpResult>();
            Resp->bOk = false;
            Resp->Status = 500;

            FPlayloopBugReportResult Result;
            PlayloopBugReport::Submit(
                TEXT("title"), TEXT("desc"), EPlayloopBugSeverity::Medium, FPlayloopBugReportParams(),
                EnabledEnv(), MakeSender(Calls, Url, Body, Resp),
                [&Result](const FPlayloopBugReportResult& R) { Result = R; });

            TestFalse("not ok", Result.bOk);
            TestEqual("http_error", Result.ErrorCode, FString(TEXT("http_error")));
        });
    });
}

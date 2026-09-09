#include "PlayloopBugReport.h"

#include "Dom/JsonObject.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PlayloopBugReport
{
    const TCHAR* SeverityToWire(EPlayloopBugSeverity Severity)
    {
        switch (Severity)
        {
            case EPlayloopBugSeverity::Low:      return TEXT("low");
            case EPlayloopBugSeverity::Medium:   return TEXT("medium");
            case EPlayloopBugSeverity::High:     return TEXT("high");
            case EPlayloopBugSeverity::Critical: return TEXT("critical");
        }
        return TEXT("medium");
    }

    FString ResolveUrl(const FString& ApiBaseUrl)
    {
        return FString::Printf(TEXT("%s/api/telemetry/bug-report"), *ApiBaseUrl);
    }

    FString ResolveIdempotencyKey(const FString& Supplied)
    {
        if (!Supplied.IsEmpty())
        {
            return Supplied;
        }
        return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    FPlayloopBugReportContext ResolveContext(const FPlayloopBugReportContext& Supplied, const FEnv& Env)
    {
        FPlayloopBugReportContext Out;

        // Supplied values win field by field; otherwise the client's own
        // telemetry sources fill build version / environment / platform.
        const FString BuildVersion = !Supplied.BuildVersion.IsEmpty() ? Supplied.BuildVersion : Env.AutoBuildVersion;
        const FString Environment  = !Supplied.Environment.IsEmpty()  ? Supplied.Environment  : Env.AutoEnvironment;
        const FString Platform     = !Supplied.Platform.IsEmpty()     ? Supplied.Platform     : Env.AutoPlatform;

        // Clamp to the server caps so an over-long auto-filled value never
        // trips a server-side reject on optional context.
        Out.BuildVersion = BuildVersion.Left(kBuildVersionMax);
        Out.Environment  = Environment.Left(kEnvironmentMax);
        Out.Platform     = Platform.Left(kPlatformMax);
        Out.Device       = Supplied.Device;
        return Out;
    }

    bool BuildRequestBody(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FString& ResolvedSessionId,
        const FString& PlayerId,
        const FString& ResolvedIdempotencyKey,
        const FPlayloopBugReportContext& Context,
        FString& OutBody,
        FPlayloopBugReportResult& OutError)
    {
        // Client-side validation: title/description are required and length-
        // capped. A caller bug (empty/over-long) is a real bug regardless of
        // configuration, so this fires before the disabled-client soft-fail.
        if (Title.IsEmpty())
        {
            OutError.bOk = false;
            OutError.ErrorCode = TEXT("invalid_argument");
            OutError.ErrorMessage = TEXT("Title is required (non-empty).");
            return false;
        }
        if (Title.Len() > kTitleMax)
        {
            OutError.bOk = false;
            OutError.ErrorCode = TEXT("invalid_argument");
            OutError.ErrorMessage = FString::Printf(TEXT("Title must be at most %d characters."), kTitleMax);
            return false;
        }
        if (Description.IsEmpty())
        {
            OutError.bOk = false;
            OutError.ErrorCode = TEXT("invalid_argument");
            OutError.ErrorMessage = TEXT("Description is required (non-empty).");
            return false;
        }
        if (Description.Len() > kDescriptionMax)
        {
            OutError.bOk = false;
            OutError.ErrorCode = TEXT("invalid_argument");
            OutError.ErrorMessage = FString::Printf(TEXT("Description must be at most %d characters."), kDescriptionMax);
            return false;
        }

        TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
        Body->SetStringField(TEXT("title"), Title);
        Body->SetStringField(TEXT("description"), Description);
        Body->SetStringField(TEXT("severity"), SeverityToWire(Severity));
        Body->SetStringField(TEXT("idempotencyKey"), ResolvedIdempotencyKey);
        if (!ResolvedSessionId.IsEmpty())
        {
            Body->SetStringField(TEXT("sessionId"), ResolvedSessionId);
        }
        if (!PlayerId.IsEmpty())
        {
            Body->SetStringField(TEXT("playerId"), PlayerId);
        }

        // Only emit context when at least one field is present.
        TSharedPtr<FJsonObject> Ctx = MakeShared<FJsonObject>();
        bool bHasContext = false;
        if (!Context.BuildVersion.IsEmpty()) { Ctx->SetStringField(TEXT("buildVersion"), Context.BuildVersion); bHasContext = true; }
        if (!Context.Environment.IsEmpty())  { Ctx->SetStringField(TEXT("environment"), Context.Environment);   bHasContext = true; }
        if (!Context.Platform.IsEmpty())     { Ctx->SetStringField(TEXT("platform"), Context.Platform);         bHasContext = true; }
        if (Context.Device.Num() > 0)
        {
            TSharedPtr<FJsonObject> Device = MakeShared<FJsonObject>();
            for (const TPair<FString, FString>& Pair : Context.Device)
            {
                Device->SetStringField(Pair.Key, Pair.Value);
            }
            Ctx->SetObjectField(TEXT("device"), Device);
            bHasContext = true;
        }
        if (bHasContext)
        {
            Body->SetObjectField(TEXT("context"), Ctx);
        }

        OutBody.Reset();
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutBody);
        FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
        return true;
    }

    FPlayloopBugReportResult MapHttpResult(const FPlayloopHttpResult& Result)
    {
        FPlayloopBugReportResult Out;
        Out.Status = Result.Status;

        // Empty body: nothing to read. A transport failure with no body is a
        // plain HTTP error (network drop / 5xx with no payload).
        if (Result.Body.IsEmpty())
        {
            Out.bOk = false;
            Out.ErrorCode = TEXT("http_error");
            Out.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Result.Status);
            return Out;
        }

        // Parse the body even on a non-2xx: the server's reject envelope is
        // { "error": "<message>" } (some responses add more fields), and the
        // caller wants that message, not a bare "http_error" (parity with the
        // sibling SDKs, which read the same fields with the same fallbacks).
        TSharedPtr<FJsonObject> Parsed;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Body);
        if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
        {
            Out.bOk = false;
            // A non-JSON body on a failed request is still just an HTTP error;
            // an unexpected non-JSON body on a 2xx is a parse error.
            Out.ErrorCode = Result.bOk ? TEXT("parse_error") : TEXT("http_error");
            Out.ErrorMessage = Result.bOk
                ? TEXT("Server response was not valid JSON.")
                : FString::Printf(TEXT("HTTP %d"), Result.Status);
            return Out;
        }

        const bool bServerOk = Parsed->HasField(TEXT("ok")) && Parsed->GetBoolField(TEXT("ok"));
        if (!bServerOk)
        {
            Out.bOk = false;
            Out.ErrorCode = Parsed->HasField(TEXT("reason"))
                ? Parsed->GetStringField(TEXT("reason"))
                : (Result.bOk ? TEXT("unknown") : TEXT("http_error"));
            Out.ErrorMessage = Parsed->HasField(TEXT("message"))
                ? Parsed->GetStringField(TEXT("message"))
                : (Parsed->HasField(TEXT("error"))
                    ? Parsed->GetStringField(TEXT("error"))
                    : TEXT("Server rejected the bug report."));
            return Out;
        }

        Out.bOk = true;
        if (Parsed->HasField(TEXT("bugReportId")))
        {
            Out.BugReportId = Parsed->GetStringField(TEXT("bugReportId"));
        }
        Out.RoutingStatus = Parsed->HasField(TEXT("routingStatus"))
            ? Parsed->GetStringField(TEXT("routingStatus"))
            : TEXT("pending");
        Out.bIdempotent = Parsed->HasField(TEXT("idempotent")) && Parsed->GetBoolField(TEXT("idempotent"));
        return Out;
    }

    void Submit(
        const FString& Title,
        const FString& Description,
        EPlayloopBugSeverity Severity,
        const FPlayloopBugReportParams& Params,
        const FEnv& Env,
        const FSender& Sender,
        TFunction<void(const FPlayloopBugReportResult&)> OnDone)
    {
        auto Deliver = [&OnDone](const FPlayloopBugReportResult& Result)
        {
            if (OnDone) OnDone(Result);
        };

        // Resolve the idempotency key ONCE, before the (retrying) transport, so
        // a redelivery re-sends the same key and the server dedupes.
        const FString IdempotencyKey = ResolveIdempotencyKey(Params.IdempotencyKey);

        // An explicit session id wins; otherwise attribute to the launch's
        // current session (empty = omit, the report just isn't session-scoped).
        const FString SessionId = !Params.SessionId.IsEmpty() ? Params.SessionId : Env.ResolvedSessionId;

        const FPlayloopBugReportContext Context = ResolveContext(Params.Context, Env);

        // Validate + build the wire body first: a caller bug (empty/over-long
        // title or description) is a real bug even for a disabled client.
        FString Body;
        FPlayloopBugReportResult ValidationError;
        if (!BuildRequestBody(Title, Description, Severity, SessionId, Params.PlayerId,
                              IdempotencyKey, Context, Body, ValidationError))
        {
            Deliver(ValidationError);
            return;
        }

        // Disabled client (no API key): resolve to a soft failure rather than
        // hitting the network. The caller branches on bOk exactly as it would
        // for a server reject. Mirrors the Player Feedback surface.
        if (!Env.bEnabled)
        {
            FPlayloopBugReportResult Disabled;
            Disabled.bOk = false;
            Disabled.ErrorCode = TEXT("disabled");
            Disabled.ErrorMessage = TEXT("Playloop client is disabled (no API key configured); bug report not sent.");
            Deliver(Disabled);
            return;
        }

        if (!Sender)
        {
            FPlayloopBugReportResult NoSender;
            NoSender.bOk = false;
            NoSender.ErrorCode = TEXT("http_error");
            NoSender.ErrorMessage = TEXT("No transport available.");
            Deliver(NoSender);
            return;
        }

        const FString Url = ResolveUrl(Env.ApiBaseUrl);
        Sender(Url, Body, [OnDone](const FPlayloopHttpResult& HttpResult)
        {
            const FPlayloopBugReportResult Mapped = MapHttpResult(HttpResult);
            if (OnDone) OnDone(Mapped);
        });
    }
}

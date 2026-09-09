#include "PlayloopEditorResolve.h"
#include "PlayloopSettings.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PlayloopEditorResolve
{
    void Resolve(FOnResolved OnDone)
    {
        const UPlayloopSettings* S = GetDefault<UPlayloopSettings>();
        if (!S || S->ApiKey.IsEmpty())
        {
            OnDone.ExecuteIfBound(false, FString(), FString());
            return;
        }

        const FString Url = S->ApiBaseUrl.TrimEnd() / TEXT("api/telemetry/resolve");
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
        Req->SetVerb(TEXT("GET"));
        Req->SetURL(Url);
        Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *S->ApiKey));
        Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
        Req->OnProcessRequestComplete().BindLambda(
            [OnDone](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
            {
                const int32 Code = (bOk && Resp.IsValid()) ? Resp->GetResponseCode() : 0;
                if (Code != 200 || !Resp.IsValid())
                {
                    OnDone.ExecuteIfBound(false, FString(), FString());
                    return;
                }
                FString Slug, Name;
                TSharedPtr<FJsonObject> Json;
                const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
                {
                    Json->TryGetStringField(TEXT("slug"), Slug);
                    Json->TryGetStringField(TEXT("name"), Name);
                }
                OnDone.ExecuteIfBound(true, Slug, Name);
            });
        Req->ProcessRequest();
    }
}

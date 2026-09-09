#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopExperiments.h"

// Covers the four experiment behaviors using an injected, synchronous
// fetcher so each spec is deterministic and needs no real network:
//   1. Lazy fetch: no fetch until the first VariantAsync().
//   2. Session cache: a known id is answered from cache, no refetch.
//   3. Refetch-once: an unknown id costs exactly one extra fetch,
//                           then the negative answer is cached.
//   4. Last-good on error: a failed fetch keeps the prior cache; a failed
//                           first fetch resolves to an empty string.

BEGIN_DEFINE_SPEC(FPlayloopExperimentsSpec, "Playloop.Experiments",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)

    // Builds a fetcher that completes synchronously, counting invocations.
    static UPlayloopExperiments::FFetcher MakeFetcher(
        TSharedRef<int32> Counter,
        TSharedRef<bool> bOk,
        TSharedRef<TMap<FString, FString>> Response)
    {
        return [Counter, bOk, Response](UPlayloopExperiments::FFetchComplete Complete)
        {
            (*Counter)++;
            Complete.ExecuteIfBound(*bOk, *Response, FPlayloopVariantConfigMap());
        };
    }

    // Builds a fetcher that completes synchronously with both the assignment
    // map and a parallel config map.
    static UPlayloopExperiments::FFetcher MakeFetcherWithConfig(
        TSharedRef<int32> Counter,
        TSharedRef<TMap<FString, FString>> Response,
        TSharedRef<FPlayloopVariantConfigMap> ConfigResponse)
    {
        return [Counter, Response, ConfigResponse](UPlayloopExperiments::FFetchComplete Complete)
        {
            (*Counter)++;
            Complete.ExecuteIfBound(true, *Response, *ConfigResponse);
        };
    }

END_DEFINE_SPEC(FPlayloopExperimentsSpec)

void FPlayloopExperimentsSpec::Define()
{
    Describe("Lazy fetch", [this]()
    {
        It("does not fetch until the first VariantAsync call", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("treatment"));
            Exp->SetFetcherForTesting(MakeFetcher(Count, Ok, Resp));

            TestEqual("no fetch before any call", *Count, 0);

            FString Result;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&Result](const FString& V) { Result = V; }));

            TestEqual("first VariantAsync triggers one fetch", *Count, 1);
            TestEqual("resolves the assigned variant", Result, FString(TEXT("treatment")));
        });
    });

    Describe("Session-scoped cache", [this]()
    {
        It("answers a known id from cache without refetching", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("control"));
            Exp->SetFetcherForTesting(MakeFetcher(Count, Ok, Resp));

            FString First, Second;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&First](const FString& V) { First = V; }));
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&Second](const FString& V) { Second = V; }));

            TestEqual("only one fetch for repeated known id", *Count, 1);
            TestEqual("first answer", First, FString(TEXT("control")));
            TestEqual("second answer from cache", Second, FString(TEXT("control")));

            // Synchronous cached-only read agrees with the async path.
            TestEqual("sync Variant() reads cache", Exp->Variant(TEXT("exp_a")), FString(TEXT("control")));
        });

        It("sync Variant() returns empty before the cache is warm", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TestEqual("cold sync read is empty", Exp->Variant(TEXT("exp_a")), FString());
        });
    });

    Describe("Refetch-once on unknown id", [this]()
    {
        It("refetches exactly once per unknown id, then caches the negative", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            // Server never knows this id. Map stays empty.
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Exp->SetFetcherForTesting(MakeFetcher(Count, Ok, Resp));

            FString First;
            Exp->VariantAsync(TEXT("exp_missing"), FOnPlayloopVariantResolved::CreateLambda(
                [&First](const FString& V) { First = V; }));
            // Lazy initial fetch (1) + one refetch for the unknown id (2).
            TestEqual("initial fetch + one refetch", *Count, 2);
            TestEqual("unknown id resolves empty", First, FString());

            FString Second;
            Exp->VariantAsync(TEXT("exp_missing"), FOnPlayloopVariantResolved::CreateLambda(
                [&Second](const FString& V) { Second = V; }));
            // Negative answer is cached. No third fetch.
            TestEqual("no further fetch for same unknown id", *Count, 2);
            TestEqual("still empty", Second, FString());
        });

        It("picks up an experiment created between the initial fetch and the refetch", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            // The experiment doesn't exist on the initial fetch but is
            // created before the one allowed refetch fires.
            TSharedRef<int32> Calls = MakeShared<int32>(0);
            Exp->SetFetcherForTesting(
                [Calls](UPlayloopExperiments::FFetchComplete Complete)
                {
                    (*Calls)++;
                    TMap<FString, FString> Map;
                    if (*Calls >= 2)
                    {
                        Map.Add(TEXT("exp_new"), TEXT("variant-b"));
                    }
                    Complete.ExecuteIfBound(true, Map, FPlayloopVariantConfigMap());
                });

            FString Result;
            Exp->VariantAsync(TEXT("exp_new"), FOnPlayloopVariantResolved::CreateLambda(
                [&Result](const FString& V) { Result = V; }));
            TestEqual("initial fetch + one refetch", *Calls, 2);
            TestEqual("found on the refetch", Result, FString(TEXT("variant-b")));
        });
    });

    Describe("Last-good on network error", [this]()
    {
        It("keeps the prior cache when a later refresh fails", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<bool> Ok = MakeShared<bool>(true);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("treatment"));
            Exp->SetFetcherForTesting(MakeFetcher(Count, Ok, Resp));

            FString First;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&First](const FString& V) { First = V; }));
            TestEqual("cached on success", First, FString(TEXT("treatment")));

            // Network goes down on a later refresh.
            *Ok = false;
            bool bRefreshed = false;
            Exp->RefreshAsync(FOnPlayloopExperimentsRefreshed::CreateLambda(
                [&bRefreshed]() { bRefreshed = true; }));
            TestTrue("refresh completes (never blocks)", bRefreshed);

            // Last-good still answers.
            TestEqual("sync read survives failed refresh", Exp->Variant(TEXT("exp_a")), FString(TEXT("treatment")));
            FString AfterFailure;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&AfterFailure](const FString& V) { AfterFailure = V; }));
            TestEqual("async read survives failed refresh", AfterFailure, FString(TEXT("treatment")));
        });

        It("resolves empty when the very first fetch fails", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<bool> Ok = MakeShared<bool>(false);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Exp->SetFetcherForTesting(MakeFetcher(Count, Ok, Resp));

            FString Result;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&Result](const FString& V) { Result = V; }));
            TestEqual("first-fetch failure resolves empty", Result, FString());
        });
    });

    Describe("Variant config payloads", [this]()
    {
        // Builds a config map with one experiment carrying string / number /
        // bool values.
        auto MakeConfig = []()
        {
            FPlayloopConfigValue StringVal;
            StringVal.Type = EPlayloopConfigValueType::String;
            StringVal.StringValue = TEXT("Bravo");
            FPlayloopConfigValue NumberVal;
            NumberVal.Type = EPlayloopConfigValueType::Number;
            NumberVal.NumberValue = 15.0;
            FPlayloopConfigValue BoolVal;
            BoolVal.Type = EPlayloopConfigValueType::Bool;
            BoolVal.BoolValue = true;

            FPlayloopVariantConfig VariantConfig;
            VariantConfig.Values.Add(TEXT("cta_label"), StringVal);
            VariantConfig.Values.Add(TEXT("enemy_damage"), NumberVal);
            VariantConfig.Values.Add(TEXT("show_hints"), BoolVal);

            TSharedRef<FPlayloopVariantConfigMap> ConfigMap = MakeShared<FPlayloopVariantConfigMap>();
            ConfigMap->Add(TEXT("exp_a"), VariantConfig);
            return ConfigMap;
        };

        It("exposes the assigned variant config through typed getters", [this, MakeConfig]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("treatment"));
            Exp->SetFetcherForTesting(MakeFetcherWithConfig(Count, Resp, MakeConfig()));

            FPlayloopVariantConfig Config;
            Exp->GetConfigAsync(TEXT("exp_a"), FOnPlayloopVariantConfigResolved::CreateLambda(
                [&Config](const FPlayloopVariantConfig& C) { Config = C; }));

            TestEqual("string value", Config.GetString(TEXT("cta_label"), TEXT("x")), FString(TEXT("Bravo")));
            TestEqual("int value", Config.GetInt(TEXT("enemy_damage"), 0), 15);
            TestEqual("float value", Config.GetFloat(TEXT("enemy_damage"), 0.0f), 15.0f);
            TestTrue("bool value", Config.GetBool(TEXT("show_hints"), false));
            // Missing keys fall back.
            TestEqual("missing string falls back", Config.GetString(TEXT("nope"), TEXT("dflt")), FString(TEXT("dflt")));
            TestEqual("missing int falls back", Config.GetInt(TEXT("nope"), 99), 99);
        });

        It("resolves an empty config when the variant carries none", [this]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("control"));
            // Config map has an entry for a different experiment only.
            TSharedRef<FPlayloopVariantConfigMap> ConfigMap = MakeShared<FPlayloopVariantConfigMap>();
            FPlayloopVariantConfig Other;
            FPlayloopConfigValue V; V.Type = EPlayloopConfigValueType::Number; V.NumberValue = 1.0;
            Other.Values.Add(TEXT("x"), V);
            ConfigMap->Add(TEXT("exp_b"), Other);
            Exp->SetFetcherForTesting(MakeFetcherWithConfig(Count, Resp, ConfigMap));

            FPlayloopVariantConfig Config;
            bool bResolved = false;
            Exp->GetConfigAsync(TEXT("exp_a"), FOnPlayloopVariantConfigResolved::CreateLambda(
                [&Config, &bResolved](const FPlayloopVariantConfig& C) { Config = C; bResolved = true; }));

            TestTrue("resolved", bResolved);
            TestEqual("empty config for a variant with no payload", Config.Num(), 0);
        });

        It("shares the lazy fetch and cache with VariantAsync", [this, MakeConfig]()
        {
            UPlayloopExperiments* Exp = NewObject<UPlayloopExperiments>();
            TSharedRef<int32> Count = MakeShared<int32>(0);
            TSharedRef<TMap<FString, FString>> Resp = MakeShared<TMap<FString, FString>>();
            Resp->Add(TEXT("exp_a"), TEXT("treatment"));
            Exp->SetFetcherForTesting(MakeFetcherWithConfig(Count, Resp, MakeConfig()));

            FString Variant;
            Exp->VariantAsync(TEXT("exp_a"), FOnPlayloopVariantResolved::CreateLambda(
                [&Variant](const FString& V) { Variant = V; }));
            FPlayloopVariantConfig Config;
            Exp->GetConfigAsync(TEXT("exp_a"), FOnPlayloopVariantConfigResolved::CreateLambda(
                [&Config](const FPlayloopVariantConfig& C) { Config = C; }));

            TestEqual("variant resolved", Variant, FString(TEXT("treatment")));
            TestEqual("config read from the same fetch", Config.GetInt(TEXT("enemy_damage"), 0), 15);
            // One fetch total: GetConfig reused the cache VariantAsync warmed.
            TestEqual("single fetch shared", *Count, 1);

            // Synchronous cached-only read agrees.
            TestEqual("sync GetConfig reads cache", Exp->GetConfig(TEXT("exp_a")).GetInt(TEXT("enemy_damage"), 0), 15);
        });
    });
}

#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopEventConfigStore.h"
#include "PlayloopGameResolver.h"
#include "PlayloopHttpClient.h"

BEGIN_DEFINE_SPEC(FPlayloopEventConfigStoreSpec, "Playloop.Client.EventConfig",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopEventConfigStoreSpec)

void FPlayloopEventConfigStoreSpec::Define()
{
    Describe("Event-config store", [this]()
    {
        It("returns safe defaults before any fetch", [this]()
        {
            TSharedPtr<FPlayloopHttpClient> Http = MakeShared<FPlayloopHttpClient>();
            TSharedPtr<FPlayloopGameResolver> Resolver = MakeShared<FPlayloopGameResolver>(
                Http, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);
            TSharedPtr<FPlayloopEventConfigStore> Store = MakeShared<FPlayloopEventConfigStore>(
                Http, Resolver, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);

            TestFalse("not loaded yet", Store->IsLoaded());

            FPlayloopGameConfigBlock Game = Store->GameSettings();
            TestTrue("branding defaults on", Game.bShowBranding);
            TestTrue("crashes default on", Game.bCrashesEnabled);
            TestEqual("crash count default", Game.CrashRecentEventsCount, 5);

            FPlayloopEventConfigEntry Entry;
            TestFalse("unknown event has no row", Store->Lookup(TEXT("level_complete"), Entry));
            TestFalse("empty event name has no row", Store->Lookup(TEXT(""), Entry));
        });

        It("stays inert when the ingest key can't be resolved to a game", [this]()
        {
            TSharedPtr<FPlayloopHttpClient> Http = MakeShared<FPlayloopHttpClient>();
            TSharedPtr<FPlayloopGameResolver> Resolver = MakeShared<FPlayloopGameResolver>(
                Http, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);
            // Stub the resolve to fail synchronously (empty identity).
            Resolver->SetResolverForTesting(
                [](FPlayloopGameResolver::FResolveComplete Complete)
                {
                    Complete.ExecuteIfBound(FPlayloopResolvedGame());
                });
            TSharedPtr<FPlayloopEventConfigStore> Store = MakeShared<FPlayloopEventConfigStore>(
                Http, Resolver, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);

            // Refresh() resolves first; an unresolvable key means no fetch and
            // the store stays unloaded (no crash).
            Store->Refresh();
            TestFalse("still unloaded", Store->IsLoaded());
        });
    });
}

#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopGameResolver.h"
#include "PlayloopHttpClient.h"

// Covers the resolver contract with an injected synchronous resolver so each
// spec is deterministic and needs no real network:
//   - Lazy + cached: one resolve serves repeated callers.
//   - Success shape: gameId + slug + name surface to the caller.
//   - Failure caching: an empty/failed resolve reports bOk=false, cached.

BEGIN_DEFINE_SPEC(FPlayloopGameResolverSpec, "Playloop.GameResolver",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopGameResolverSpec)

void FPlayloopGameResolverSpec::Define()
{
    Describe("Game resolver", [this]()
    {
        It("resolves the game identity and caches it across callers", [this]()
        {
            TSharedPtr<FPlayloopHttpClient> Http = MakeShared<FPlayloopHttpClient>();
            TSharedPtr<FPlayloopGameResolver> Resolver = MakeShared<FPlayloopGameResolver>(
                Http, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);

            TSharedRef<int32> Count = MakeShared<int32>(0);
            Resolver->SetResolverForTesting(
                [Count](FPlayloopGameResolver::FResolveComplete Complete)
                {
                    (*Count)++;
                    FPlayloopResolvedGame Game;
                    Game.GameId = TEXT("g_abc");
                    Game.Slug = TEXT("my-game");
                    Game.Name = TEXT("My Game");
                    Complete.ExecuteIfBound(Game);
                });

            FString FirstId, FirstSlug, FirstName;
            bool bFirstOk = false;
            Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
                [&](bool bOk, const FPlayloopResolvedGame& Game)
                {
                    bFirstOk = bOk; FirstId = Game.GameId; FirstSlug = Game.Slug; FirstName = Game.Name;
                }));
            TestTrue("first resolve ok", bFirstOk);
            TestEqual("gameId", FirstId, FString(TEXT("g_abc")));
            TestEqual("slug", FirstSlug, FString(TEXT("my-game")));
            TestEqual("name", FirstName, FString(TEXT("My Game")));
            TestEqual("one network resolve", *Count, 1);

            bool bSecondOk = false;
            Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
                [&](bool bOk, const FPlayloopResolvedGame&) { bSecondOk = bOk; }));
            TestTrue("second resolve ok (from cache)", bSecondOk);
            TestEqual("still one resolve (cached)", *Count, 1);
        });

        It("reports failure (bOk=false) and caches it when the key can't be resolved", [this]()
        {
            TSharedPtr<FPlayloopHttpClient> Http = MakeShared<FPlayloopHttpClient>();
            TSharedPtr<FPlayloopGameResolver> Resolver = MakeShared<FPlayloopGameResolver>(
                Http, TEXT("pl_ik_x"), TEXT("https://playloop.gg"), TEXT("production"), 3);

            TSharedRef<int32> Count = MakeShared<int32>(0);
            Resolver->SetResolverForTesting(
                [Count](FPlayloopGameResolver::FResolveComplete Complete)
                {
                    (*Count)++;
                    Complete.ExecuteIfBound(FPlayloopResolvedGame()); // empty == failure
                });

            bool bFirstOk = true;
            Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
                [&](bool bOk, const FPlayloopResolvedGame&) { bFirstOk = bOk; }));
            TestFalse("resolve reported failure", bFirstOk);

            bool bSecondOk = true;
            Resolver->ResolveAsync(FOnPlayloopGameResolved::CreateLambda(
                [&](bool bOk, const FPlayloopResolvedGame&) { bSecondOk = bOk; }));
            TestFalse("failure is cached", bSecondOk);
            TestEqual("no retry on the hot path", *Count, 1);
        });
    });
}

#include "PlayloopEditorPaths.h"

namespace PlayloopEditorPaths
{
    FString DashboardForGame(const FString& Slug, const FString& Env)
    {
        // No slug (key couldn't be resolved) → fall back to the games list.
        FString Url = Slug.IsEmpty()
            ? FString::Printf(TEXT("%s/games"), DashboardBaseUrl)
            : FString::Printf(TEXT("%s/games/%s"), DashboardBaseUrl, *Slug);
        if (!Env.IsEmpty() && !Env.Equals(TEXT("production"), ESearchCase::IgnoreCase))
        {
            Url += FString::Printf(TEXT("?env=%s"), *Env);
        }
        return Url;
    }
}

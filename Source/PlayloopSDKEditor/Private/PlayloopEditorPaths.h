#pragma once

#include "CoreMinimal.h"

namespace PlayloopEditorPaths
{
    inline const TCHAR* DashboardBaseUrl = TEXT("https://playloop.gg");
    inline const TCHAR* DocsUrl = TEXT("https://playloop.gg/docs");
    inline const TCHAR* RoadmapUrl = TEXT("https://playloop.gg/roadmap");
    inline const TCHAR* RepoUrl = TEXT("https://github.com/playloop/sdk-unreal");
    inline const TCHAR* DemoUrl = TEXT("https://playloop.gg/demo");

    FString DashboardForGame(const FString& Slug, const FString& Env);
}

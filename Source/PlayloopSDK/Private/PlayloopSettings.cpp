#include "PlayloopSettings.h"

UPlayloopSettings::UPlayloopSettings()
{
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("Playloop");
}

FString UPlayloopSettings::ResolvedEnvironment() const
{
    if (Environment.Equals(TEXT("custom"), ESearchCase::IgnoreCase))
    {
        return CustomEnvironmentOverride.IsEmpty() ? TEXT("custom") : CustomEnvironmentOverride;
    }
    return Environment;
}

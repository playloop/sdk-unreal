#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

PLAYLOOPSDK_API DECLARE_LOG_CATEGORY_EXTERN(LogPlayloop, Log, All);

/**
 * Runtime module. Loads in PreDefault so the crash trap is registered
 * before any project module that might crash during init.
 */
class FPlayloopSDKModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

#include "PlayloopSDK.h"
#include "PlayloopCrashHandler.h"

DEFINE_LOG_CATEGORY(LogPlayloop);

#define LOCTEXT_NAMESPACE "PlayloopSDK"

void FPlayloopSDKModule::StartupModule()
{
    // Deliberately does NOT install the crash trap here. A game that links the
    // plugin but never configures Playloop should leave no footprint. an
    // always-installed trap would write Saved/Playloop/pending-crashes.json
    // that nothing ever drains. The trap is installed on demand from
    // UPlayloopClient::Start() when bEnableCrashHandler is set, so it only
    // exists once there's a live client that will drain it.
    UE_LOG(LogPlayloop, Log, TEXT("Playloop SDK starting (PreDefault)."));
}

void FPlayloopSDKModule::ShutdownModule()
{
    // Idempotent: no-ops when the trap was never installed (Playloop unused).
    FPlayloopCrashHandler::Get().Shutdown();
    UE_LOG(LogPlayloop, Log, TEXT("Playloop SDK stopped."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayloopSDKModule, PlayloopSDK)

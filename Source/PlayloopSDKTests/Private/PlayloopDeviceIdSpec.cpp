#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformFileManager.h"
#include "PlayloopDeviceId.h"

BEGIN_DEFINE_SPEC(FPlayloopDeviceIdSpec, "Playloop.DeviceId",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopDeviceIdSpec)

void FPlayloopDeviceIdSpec::Define()
{
    Describe("Resolve()", [this]()
    {
        It("returns a stable id across calls within one session", [this]()
        {
            // Clean cached + on-disk id.
            FPlayloopDeviceId::OverrideForTesting(FString());
            IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
            File.DeleteFile(*FPlayloopDeviceId::GetPersistPath());

            const FString First = FPlayloopDeviceId::Resolve();
            const FString Second = FPlayloopDeviceId::Resolve();
            TestEqual("stable within session", First, Second);
            TestTrue("non-empty", !First.IsEmpty());
        });

        It("honors a test override", [this]()
        {
            FPlayloopDeviceId::OverrideForTesting(TEXT("dev-override-test"));
            TestEqual("override returned", FPlayloopDeviceId::Resolve(), FString(TEXT("dev-override-test")));
            FPlayloopDeviceId::OverrideForTesting(FString());
        });
    });
}

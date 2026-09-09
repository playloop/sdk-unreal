#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformFileManager.h"
#include "PlayloopCrashHandler.h"

BEGIN_DEFINE_SPEC(FPlayloopCrashHandlerSpec, "Playloop.CrashHandler",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopCrashHandlerSpec)

void FPlayloopCrashHandlerSpec::Define()
{
    Describe("ReportManagedCrash -> DrainPersisted round-trip", [this]()
    {
        It("persists a crash json and reads it back, then clears the file", [this]()
        {
            FPlayloopCrashHandler& Handler = FPlayloopCrashHandler::Get();

            // Pre-clean.
            IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
            File.DeleteFile(*FPlayloopCrashHandler::GetPersistPath());

            FPlayloopCrashContext Ctx;
            Ctx.CrashedSessionId = TEXT("ps_test");
            Ctx.BuildVersion = TEXT("1.0.0");
            Ctx.SdkVersion = TEXT("playloop-unreal@0.5.0");
            Ctx.Platform = TEXT("Unreal/Mac");
            Handler.SetCurrentContext(Ctx);

            Handler.ReportManagedCrash(TEXT("NullDereference"), TEXT("at Foo::Bar() line 42"));
            Handler.ReportManagedCrash(TEXT("Assertion"), TEXT("at Spam::Eggs() line 7"));

            TArray<TSharedPtr<FJsonObject>> Drained = Handler.DrainPersisted();
            TestEqual("two crashes drained", Drained.Num(), 2);

            TSharedPtr<FJsonObject> First = Drained[0];
            TestEqual("session id stamped", First->GetStringField(TEXT("crashedSessionId")), FString(TEXT("ps_test")));
            TestEqual("build version stamped", First->GetStringField(TEXT("buildVersion")), FString(TEXT("1.0.0")));
            TestEqual("message preserved", First->GetStringField(TEXT("message")), FString(TEXT("NullDereference")));
            TestTrue("stack present", First->GetStringField(TEXT("stackTrace")).Contains(TEXT("Foo::Bar")));

            // Second drain should be empty (file removed).
            TArray<TSharedPtr<FJsonObject>> Empty = Handler.DrainPersisted();
            TestEqual("file cleared after drain", Empty.Num(), 0);
        });

    });

    Describe("Crash-trap install gating", [this]()
    {
        It("stays uninstalled until explicitly initialized, then toggles idempotently", [this]()
        {
            FPlayloopCrashHandler& Handler = FPlayloopCrashHandler::Get();

            // The module no longer installs the trap at load, so a client that
            // never started leaves it uninstalled. Ensure a clean baseline.
            Handler.Shutdown();
            TestFalse("uninstalled by default", Handler.IsInstalled());

            Handler.Initialize();
            TestTrue("installed after Initialize", Handler.IsInstalled());

            // Idempotent: a second Initialize (Start()/Configure() churn) is a no-op.
            Handler.Initialize();
            TestTrue("still installed after repeat Initialize", Handler.IsInstalled());

            Handler.Shutdown();
            TestFalse("uninstalled after Shutdown", Handler.IsInstalled());

            // Idempotent Shutdown too.
            Handler.Shutdown();
            TestFalse("still uninstalled after repeat Shutdown", Handler.IsInstalled());
        });
    });

    Describe("ReportManagedCrash truncation", [this]()
    {
        It("truncates oversized stack traces to 12KB", [this]()
        {
            FPlayloopCrashHandler& Handler = FPlayloopCrashHandler::Get();
            IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
            File.DeleteFile(*FPlayloopCrashHandler::GetPersistPath());

            FString Long;
            Long.Reserve(20 * 1024);
            for (int32 i = 0; i < 20 * 1024; ++i) Long.AppendChar(TEXT('x'));
            Handler.ReportManagedCrash(TEXT("Big"), Long);

            TArray<TSharedPtr<FJsonObject>> Drained = Handler.DrainPersisted();
            TestEqual("one crash", Drained.Num(), 1);
            const FString Stack = Drained[0]->GetStringField(TEXT("stackTrace"));
            TestTrue("stack truncated", Stack.Len() <= FPlayloopCrashHandler::MaxStackBytes);
        });
    });
}

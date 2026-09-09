#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"

/**
 * Captures uncaught crashes via FCoreDelegates::OnHandleSystemError,
 * persists them to <ProjectSavedDir>/Playloop/pending-crashes.json,
 * and drains them on next session start so they ship as
 * `crash_reported` events.
 *
 * UE5 fires OnHandleSystemError on the game thread right before the
 * engine dies. File I/O is technically allowed but kept tiny on
 * purpose.
 */
struct FPlayloopCrashContext
{
    FString CrashedSessionId;
    FString BuildVersion;
    FString SdkVersion;
    FString Platform;
    TArray<TSharedPtr<FJsonObject>> RecentEvents;
};

class PLAYLOOPSDK_API FPlayloopCrashHandler
{
public:
    static FPlayloopCrashHandler& Get();

    void Initialize();
    void Shutdown();

    /** Whether the OnHandleSystemError trap is currently registered. */
    bool IsInstalled() const;

    /** Called by the client when a session_start is emitted. */
    void SetCurrentContext(FPlayloopCrashContext Context);

    /** Called manually from gameplay code for caught/managed crashes. */
    void ReportManagedCrash(const FString& Message, const FString& StackTrace);

    /** Drains and returns persisted crashes (and clears the file). */
    TArray<TSharedPtr<FJsonObject>> DrainPersisted();

    /** Path to the persisted-crash file. Public for tests. */
    static FString GetPersistPath();

    /** Max stack-trace bytes persisted (12KB, matches other SDKs). */
    static constexpr int32 MaxStackBytes = 12 * 1024;
    /** Max total file size (64KB cap; oldest entries dropped). */
    static constexpr int32 MaxFileBytes = 64 * 1024;

private:
    FPlayloopCrashHandler() = default;
    void OnSystemError();
    void WriteCrash(TSharedPtr<FJsonObject> CrashJson);

    mutable FCriticalSection Mutex;
    FPlayloopCrashContext CurrentContext;
    bool bInstalled = false;
    FDelegateHandle SystemErrorHandle;
};

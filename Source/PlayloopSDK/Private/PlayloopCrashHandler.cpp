#include "PlayloopCrashHandler.h"
#include "PlayloopSDK.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    int64 NowMs()
    {
        return static_cast<int64>(FDateTime::UtcNow().ToUnixTimestamp() * 1000);
    }

    FString CapturePlatformStackTrace()
    {
        ANSICHAR Buffer[4096] = { 0 };
        FPlatformStackWalk::StackWalkAndDump(Buffer, sizeof(Buffer), 2);
        return FString(ANSI_TO_TCHAR(Buffer));
    }
}

FPlayloopCrashHandler& FPlayloopCrashHandler::Get()
{
    static FPlayloopCrashHandler Instance;
    return Instance;
}

FString FPlayloopCrashHandler::GetPersistPath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playloop"), TEXT("pending-crashes.json"));
}

void FPlayloopCrashHandler::Initialize()
{
    FScopeLock Guard(&Mutex);
    if (bInstalled)
    {
        return;
    }
    SystemErrorHandle = FCoreDelegates::OnHandleSystemError.AddRaw(this, &FPlayloopCrashHandler::OnSystemError);
    bInstalled = true;
    UE_LOG(LogPlayloop, Log, TEXT("Crash trap installed."));
}

void FPlayloopCrashHandler::Shutdown()
{
    FScopeLock Guard(&Mutex);
    if (!bInstalled)
    {
        return;
    }
    FCoreDelegates::OnHandleSystemError.Remove(SystemErrorHandle);
    SystemErrorHandle.Reset();
    bInstalled = false;
}

bool FPlayloopCrashHandler::IsInstalled() const
{
    FScopeLock Guard(&Mutex);
    return bInstalled;
}

void FPlayloopCrashHandler::SetCurrentContext(FPlayloopCrashContext Context)
{
    FScopeLock Guard(&Mutex);
    CurrentContext = MoveTemp(Context);
}

void FPlayloopCrashHandler::OnSystemError()
{
    // Inside the crash signal handler. Keep allocations small.
    const FString Stack = CapturePlatformStackTrace().Left(MaxStackBytes);

    TSharedPtr<FJsonObject> Crash = MakeShared<FJsonObject>();
    Crash->SetStringField(TEXT("stackTrace"), Stack);
    Crash->SetStringField(TEXT("message"), TEXT("Unreal system error"));
    Crash->SetNumberField(TEXT("crashedAtMs"), static_cast<double>(NowMs()));

    // Copy the context under a NON-BLOCKING lock. A blocking FScopeLock could
    // self-deadlock if the crash struck while this same thread already held
    // Mutex inside SetCurrentContext; better to ship the crash with empty
    // context than to hang the dying process. Uncontended in the common case.
    if (Mutex.TryLock())
    {
        Crash->SetStringField(TEXT("crashedSessionId"), CurrentContext.CrashedSessionId);
        Crash->SetStringField(TEXT("buildVersion"), CurrentContext.BuildVersion);
        Crash->SetStringField(TEXT("sdkVersion"), CurrentContext.SdkVersion);
        Crash->SetStringField(TEXT("platform"), CurrentContext.Platform);
        TArray<TSharedPtr<FJsonValue>> RecentArray;
        for (const TSharedPtr<FJsonObject>& E : CurrentContext.RecentEvents)
        {
            RecentArray.Add(MakeShared<FJsonValueObject>(E));
        }
        Crash->SetArrayField(TEXT("recentEvents"), RecentArray);
        Mutex.Unlock();
    }
    WriteCrash(Crash);
}

void FPlayloopCrashHandler::ReportManagedCrash(const FString& Message, const FString& StackTrace)
{
    TSharedPtr<FJsonObject> Crash = MakeShared<FJsonObject>();
    {
        FScopeLock Guard(&Mutex);
        Crash->SetStringField(TEXT("crashedSessionId"), CurrentContext.CrashedSessionId);
        Crash->SetStringField(TEXT("buildVersion"), CurrentContext.BuildVersion);
        Crash->SetStringField(TEXT("sdkVersion"), CurrentContext.SdkVersion);
        Crash->SetStringField(TEXT("platform"), CurrentContext.Platform);
        Crash->SetStringField(TEXT("stackTrace"), StackTrace.Left(MaxStackBytes));
        Crash->SetStringField(TEXT("message"), Message);
        Crash->SetNumberField(TEXT("crashedAtMs"), static_cast<double>(NowMs()));
        TArray<TSharedPtr<FJsonValue>> RecentArray;
        for (const TSharedPtr<FJsonObject>& E : CurrentContext.RecentEvents)
        {
            RecentArray.Add(MakeShared<FJsonValueObject>(E));
        }
        Crash->SetArrayField(TEXT("recentEvents"), RecentArray);
    }
    WriteCrash(Crash);
}

void FPlayloopCrashHandler::WriteCrash(TSharedPtr<FJsonObject> CrashJson)
{
    const FString Path = GetPersistPath();
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = FPaths::GetPath(Path);
    if (!Dir.IsEmpty() && !File.DirectoryExists(*Dir))
    {
        File.CreateDirectoryTree(*Dir);
    }

    // Read existing array (if any), append, cap, rewrite.
    TArray<TSharedPtr<FJsonValue>> Existing;
    FString Raw;
    if (FFileHelper::LoadFileToString(Raw, *Path))
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
        FJsonSerializer::Deserialize(Reader, Existing);
    }
    Existing.Add(MakeShared<FJsonValueObject>(CrashJson));

    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Existing, Writer);

    while (Out.Len() > MaxFileBytes && Existing.Num() > 1)
    {
        Existing.RemoveAt(0);
        Out.Reset();
        const TSharedRef<TJsonWriter<>> NextWriter = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Existing, NextWriter);
    }

    if (!FFileHelper::SaveStringToFile(Out, *Path))
    {
        UE_LOG(LogPlayloop, Warning, TEXT("Could not persist crash to %s."), *Path);
    }
}

TArray<TSharedPtr<FJsonObject>> FPlayloopCrashHandler::DrainPersisted()
{
    TArray<TSharedPtr<FJsonObject>> Out;
    const FString Path = GetPersistPath();
    FString Raw;
    if (!FFileHelper::LoadFileToString(Raw, *Path))
    {
        return Out;
    }
    TArray<TSharedPtr<FJsonValue>> Values;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
    if (FJsonSerializer::Deserialize(Reader, Values))
    {
        for (const TSharedPtr<FJsonValue>& V : Values)
        {
            if (V.IsValid() && V->Type == EJson::Object)
            {
                Out.Add(V->AsObject());
            }
        }
    }
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    File.DeleteFile(*Path);
    return Out;
}

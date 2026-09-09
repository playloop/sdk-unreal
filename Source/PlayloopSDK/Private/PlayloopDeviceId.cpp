#include "PlayloopDeviceId.h"
#include "PlayloopSDK.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
    FCriticalSection& Mutex()
    {
        static FCriticalSection Lock;
        return Lock;
    }

    FString CachedId;
}

FString FPlayloopDeviceId::GetPersistPath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playloop"), TEXT("device.id"));
}

FString FPlayloopDeviceId::Resolve()
{
    FScopeLock Guard(&Mutex());
    if (!CachedId.IsEmpty())
    {
        return CachedId;
    }

    const FString Path = GetPersistPath();
    FString FromDisk;
    if (FFileHelper::LoadFileToString(FromDisk, *Path))
    {
        FromDisk.TrimStartAndEndInline();
        if (!FromDisk.IsEmpty())
        {
            CachedId = FromDisk;
            return CachedId;
        }
    }

    const FString Fresh = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    const FString Dir = FPaths::GetPath(Path);
    if (!Dir.IsEmpty() && !File.DirectoryExists(*Dir))
    {
        File.CreateDirectoryTree(*Dir);
    }
    if (!FFileHelper::SaveStringToFile(Fresh, *Path))
    {
        UE_LOG(LogPlayloop, Warning, TEXT("Could not persist device id to %s. Falling back to in-memory id."), *Path);
    }
    CachedId = Fresh;
    return CachedId;
}

void FPlayloopDeviceId::OverrideForTesting(const FString& Id)
{
    FScopeLock Guard(&Mutex());
    CachedId = Id;
}

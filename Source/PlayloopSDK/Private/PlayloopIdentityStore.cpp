#include "PlayloopIdentityStore.h"
#include "HAL/CriticalSection.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
// UE 5.8 IWYU: the JSON print policies are no longer pulled in by JsonWriter.h.
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
    const FString DefaultConsent = TEXT("anonymous");

    FCriticalSection& Mutex()
    {
        static FCriticalSection Lock;
        return Lock;
    }

    // Per-process cache. Read-through on first call, write-through on
    // every update. Survives a failed disk write so the SDK keeps the
    // value for the lifetime of the process.
    FString CachedLinkedId;
    FString CachedConsent;
    bool bCacheLoaded = false;

    bool ParseConsentLevel(const FString& Raw, FString& Out)
    {
        if (Raw == TEXT("anonymous") || Raw == TEXT("studio-wide") ||
            Raw == TEXT("cross-platform") || Raw == TEXT("opt-out"))
        {
            Out = Raw;
            return true;
        }
        return false;
    }

    void EnsureCacheLoaded()
    {
        if (bCacheLoaded)
        {
            return;
        }
        bCacheLoaded = true;
        CachedConsent = DefaultConsent;

        FString FromDisk;
        if (!FFileHelper::LoadFileToString(FromDisk, *FPlayloopIdentityStore::GetPersistPath()))
        {
            return;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FromDisk);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return;
        }

        FString TempLinked;
        if (Root->TryGetStringField(TEXT("linkedId"), TempLinked))
        {
            CachedLinkedId = TempLinked;
        }
        FString TempConsent;
        if (Root->TryGetStringField(TEXT("consentLevel"), TempConsent))
        {
            FString Validated;
            CachedConsent = ParseConsentLevel(TempConsent, Validated) ? Validated : DefaultConsent;
        }
    }

    void PersistToDisk()
    {
        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        if (!CachedLinkedId.IsEmpty())
        {
            Root->SetStringField(TEXT("linkedId"), CachedLinkedId);
        }
        Root->SetStringField(TEXT("consentLevel"), CachedConsent);

        FString Out;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
        FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

        // Disk write is best-effort. Cache stays as the source of truth
        // for the rest of the process if this fails.
        FFileHelper::SaveStringToFile(Out, *FPlayloopIdentityStore::GetPersistPath());
    }
}

FString FPlayloopIdentityStore::GetPersistPath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playloop"), TEXT("identity.json"));
}

FString FPlayloopIdentityStore::GetLinkedId()
{
    FScopeLock Guard(&Mutex());
    EnsureCacheLoaded();
    return CachedLinkedId;
}

void FPlayloopIdentityStore::SetLinkedId(const FString& LinkedId)
{
    FScopeLock Guard(&Mutex());
    EnsureCacheLoaded();
    FString Trimmed = LinkedId;
    Trimmed.TrimStartAndEndInline();
    CachedLinkedId = Trimmed;
    PersistToDisk();
}

FString FPlayloopIdentityStore::GetConsent()
{
    FScopeLock Guard(&Mutex());
    EnsureCacheLoaded();
    return CachedConsent;
}

void FPlayloopIdentityStore::SetConsent(const FString& Level)
{
    FScopeLock Guard(&Mutex());
    EnsureCacheLoaded();
    if (Level.IsEmpty())
    {
        CachedConsent = DefaultConsent;
    }
    else
    {
        FString Validated;
        CachedConsent = ParseConsentLevel(Level, Validated) ? Validated : Level;
    }
    PersistToDisk();
}

void FPlayloopIdentityStore::ResetForTesting()
{
    FScopeLock Guard(&Mutex());
    CachedLinkedId.Empty();
    CachedConsent = DefaultConsent;
    bCacheLoaded = true;
    const FString Path = GetPersistPath();
    IFileManager::Get().Delete(*Path, false, true);
}

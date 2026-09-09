#include "PlayloopEventQueue.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
// UE 5.8 IWYU: the JSON print policies are no longer pulled in by JsonWriter.h.
#include "Policies/CondensedJsonPrintPolicy.h"

void FPlayloopEventQueue::Enqueue(FPlayloopEvent Event)
{
    FScopeLock Guard(&Mutex);
    Pending.Add(Event);
    if (Pending.Num() > MaxPending)
    {
        // Drop the oldest to keep offline memory bounded.
        Pending.RemoveAt(0, Pending.Num() - MaxPending);
    }
    Tail.Add(Event);
    if (Tail.Num() > MaxTail)
    {
        Tail.RemoveAt(0, Tail.Num() - MaxTail);
    }
}

TArray<FPlayloopEvent> FPlayloopEventQueue::DrainPending()
{
    FScopeLock Guard(&Mutex);
    TArray<FPlayloopEvent> Drained = MoveTemp(Pending);
    Pending.Reset();
    return Drained;
}

void FPlayloopEventQueue::RestoreFront(TArray<FPlayloopEvent> Events)
{
    if (Events.Num() == 0)
    {
        return;
    }
    FScopeLock Guard(&Mutex);
    TArray<FPlayloopEvent> Combined;
    Combined.Reserve(Events.Num() + Pending.Num());
    Combined.Append(MoveTemp(Events));
    Combined.Append(MoveTemp(Pending));
    if (Combined.Num() > MaxPending)
    {
        // Restored (failed) events sit at the front, so the oldest. dropped
        // first here. are the ones that have already survived the most retries.
        Combined.RemoveAt(0, Combined.Num() - MaxPending);
    }
    Pending = MoveTemp(Combined);
}

int32 FPlayloopEventQueue::PendingCount() const
{
    FScopeLock Guard(&Mutex);
    return Pending.Num();
}

TArray<FPlayloopEvent> FPlayloopEventQueue::SnapshotTail(int32 Count) const
{
    FScopeLock Guard(&Mutex);
    if (Count <= 0 || Tail.Num() == 0)
    {
        return TArray<FPlayloopEvent>();
    }
    const int32 Start = FMath::Max(0, Tail.Num() - Count);
    TArray<FPlayloopEvent> Snap;
    Snap.Reserve(Tail.Num() - Start);
    for (int32 i = Start; i < Tail.Num(); ++i)
    {
        Snap.Add(Tail[i]);
    }
    return Snap;
}

FString FPlayloopEventQueue::GenerateShortId()
{
    // Crockford base32 alphabet (no I/L/O/U to avoid look-alikes).
    static const TCHAR Alphabet[] = TEXT("0123456789ABCDEFGHJKMNPQRSTVWXYZ");
    constexpr int32 Length = 10;
    FString Out;
    Out.Reserve(Length);
    for (int32 i = 0; i < Length; ++i)
    {
        const uint32 Index = FMath::RandRange(0, 31);
        Out.AppendChar(Alphabet[Index]);
    }
    return Out;
}

namespace
{
    TSharedPtr<FJsonObject> EventToJson(const FPlayloopEvent& Event)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("id"), Event.Id);
        Obj->SetStringField(TEXT("name"), Event.Name);
        Obj->SetNumberField(TEXT("timestamp"), static_cast<double>(Event.TimestampMs));
        Obj->SetObjectField(TEXT("data"), Event.Data.IsValid() ? Event.Data : MakeShared<FJsonObject>());
        return Obj;
    }
}

FString FPlayloopEventQueue::BuildEnvelopeJson(
    const TArray<FPlayloopEvent>& Events,
    const FString& SessionId,
    const FString& DeviceId,
    const FString& Environment,
    TSharedPtr<FJsonObject> SessionMetadata,
    TSharedPtr<FJsonObject> SessionEndState,
    bool bSessionEnded,
    const FString& LinkedId,
    const FString& ConsentLevel,
    const TMap<FString, FString>& ExperimentTags)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    TArray<TSharedPtr<FJsonValue>> EventArray;
    EventArray.Reserve(Events.Num());
    for (const FPlayloopEvent& Event : Events)
    {
        EventArray.Add(MakeShared<FJsonValueObject>(EventToJson(Event)));
    }
    Root->SetArrayField(TEXT("events"), EventArray);

    if (!SessionId.IsEmpty())
    {
        Root->SetStringField(TEXT("sessionId"), SessionId);
    }
    if (!DeviceId.IsEmpty())
    {
        Root->SetStringField(TEXT("deviceId"), DeviceId);
    }
    if (!Environment.IsEmpty())
    {
        Root->SetStringField(TEXT("environment"), Environment);
    }
    if (SessionMetadata.IsValid())
    {
        Root->SetObjectField(TEXT("sessionMetadata"), SessionMetadata);
    }
    // Cross-game identity. Caller passes empty strings on
    // subsequent flushes during the same session; we only attach on
    // the first flush (when SessionId is empty server-side).
    if (!LinkedId.IsEmpty())
    {
        Root->SetStringField(TEXT("linkedId"), LinkedId);
    }
    if (!ConsentLevel.IsEmpty())
    {
        Root->SetStringField(TEXT("consentLevel"), ConsentLevel);
    }
    // A/B experiment assignments. Attached only on the
    // session-create flush (caller passes an empty map afterwards). The
    // server re-evaluates and persists only the tags that match its own
    // answer; omitted entirely when no experiments were fetched.
    if (ExperimentTags.Num() > 0)
    {
        TSharedPtr<FJsonObject> Tags = MakeShared<FJsonObject>();
        for (const TPair<FString, FString>& Pair : ExperimentTags)
        {
            Tags->SetStringField(Pair.Key, Pair.Value);
        }
        Root->SetObjectField(TEXT("experimentTags"), Tags);
    }
    if (bSessionEnded)
    {
        Root->SetBoolField(TEXT("sessionEnded"), true);
        if (SessionEndState.IsValid())
        {
            Root->SetObjectField(TEXT("sessionEndState"), SessionEndState);
        }
    }

    FString OutString;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutString);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return OutString;
}

#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PlayloopEventQueue.h"

BEGIN_DEFINE_SPEC(FPlayloopEventQueueSpec, "Playloop.EventQueue",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopEventQueueSpec)

void FPlayloopEventQueueSpec::Define()
{
    Describe("FPlayloopEventQueue::Enqueue and DrainPending", [this]()
    {
        It("returns enqueued events on drain and empties the buffer", [this]()
        {
            FPlayloopEventQueue Queue;
            FPlayloopEvent A; A.Id = TEXT("AAA"); A.Name = TEXT("a"); A.TimestampMs = 1; A.Data = MakeShared<FJsonObject>();
            FPlayloopEvent B; B.Id = TEXT("BBB"); B.Name = TEXT("b"); B.TimestampMs = 2; B.Data = MakeShared<FJsonObject>();
            Queue.Enqueue(A);
            Queue.Enqueue(B);
            TestEqual("pending after enqueue", Queue.PendingCount(), 2);
            TArray<FPlayloopEvent> Drained = Queue.DrainPending();
            TestEqual("drain returns both", Drained.Num(), 2);
            TestEqual("first id", Drained[0].Id, FString(TEXT("AAA")));
            TestEqual("second id", Drained[1].Id, FString(TEXT("BBB")));
            TestEqual("pending after drain", Queue.PendingCount(), 0);
        });
    });

    Describe("FPlayloopEventQueue::RestoreFront", [this]()
    {
        It("re-enqueues failed events at the front of the queue", [this]()
        {
            FPlayloopEventQueue Queue;
            FPlayloopEvent Old; Old.Id = TEXT("OLD"); Old.Name = TEXT("old"); Old.Data = MakeShared<FJsonObject>();
            FPlayloopEvent New; New.Id = TEXT("NEW"); New.Name = TEXT("new"); New.Data = MakeShared<FJsonObject>();
            Queue.Enqueue(New);
            Queue.RestoreFront({ Old });
            TArray<FPlayloopEvent> Drained = Queue.DrainPending();
            TestEqual("restored event first", Drained[0].Id, FString(TEXT("OLD")));
            TestEqual("newer event second", Drained[1].Id, FString(TEXT("NEW")));
        });
    });

    Describe("FPlayloopEventQueue pending cap (offline growth)", [this]()
    {
        It("drops the oldest events once Pending exceeds MaxPending", [this]()
        {
            FPlayloopEventQueue Queue;
            const int32 Total = FPlayloopEventQueue::MaxPending + 50;
            for (int32 i = 0; i < Total; ++i)
            {
                FPlayloopEvent E;
                E.Id = FString::Printf(TEXT("ID%d"), i);
                E.Name = TEXT("e");
                E.Data = MakeShared<FJsonObject>();
                Queue.Enqueue(E);
            }
            TestEqual("pending capped", Queue.PendingCount(), FPlayloopEventQueue::MaxPending);
            TArray<FPlayloopEvent> Drained = Queue.DrainPending();
            TestEqual("drain returns the cap", Drained.Num(), FPlayloopEventQueue::MaxPending);
            // The first 50 (ID0..ID49) were dropped as oldest.
            TestEqual("oldest survivor", Drained[0].Id, FString::Printf(TEXT("ID%d"), 50));
            TestEqual("newest survivor", Drained.Last().Id, FString::Printf(TEXT("ID%d"), Total - 1));
        });

        It("caps after RestoreFront re-enqueues a failed batch", [this]()
        {
            FPlayloopEventQueue Queue;
            // Current pending near the cap.
            for (int32 i = 0; i < FPlayloopEventQueue::MaxPending; ++i)
            {
                FPlayloopEvent E; E.Id = FString::Printf(TEXT("CUR%d"), i); E.Name = TEXT("e"); E.Data = MakeShared<FJsonObject>();
                Queue.Enqueue(E);
            }
            // A failed flush restores 30 older events at the front.
            TArray<FPlayloopEvent> Failed;
            for (int32 i = 0; i < 30; ++i)
            {
                FPlayloopEvent E; E.Id = FString::Printf(TEXT("OLD%d"), i); E.Name = TEXT("e"); E.Data = MakeShared<FJsonObject>();
                Failed.Add(E);
            }
            Queue.RestoreFront(Failed);
            TestEqual("still capped after restore", Queue.PendingCount(), FPlayloopEventQueue::MaxPending);
            TArray<FPlayloopEvent> Drained = Queue.DrainPending();
            // 230 -> 200 means the 30 oldest (all OLD*) are dropped; the newest
            // survivor is still the last current event.
            TestEqual("newest survivor preserved", Drained.Last().Id,
                FString::Printf(TEXT("CUR%d"), FPlayloopEventQueue::MaxPending - 1));
        });
    });

    Describe("FPlayloopEventQueue::GenerateShortId", [this]()
    {
        It("produces a 10-char Crockford base32 id with no look-alikes", [this]()
        {
            for (int32 i = 0; i < 16; ++i)
            {
                const FString Id = FPlayloopEventQueue::GenerateShortId();
                TestEqual("length", Id.Len(), 10);
                for (TCHAR C : Id)
                {
                    const bool bValid = FChar::IsDigit(C) ||
                        (C >= TEXT('A') && C <= TEXT('Z') && C != TEXT('I') && C != TEXT('L') && C != TEXT('O') && C != TEXT('U'));
                    TestTrue(FString::Printf(TEXT("char '%c' is in Crockford alphabet"), C), bValid);
                }
            }
        });
    });

    Describe("FPlayloopEventQueue::SnapshotTail", [this]()
    {
        It("returns at most N most-recent events", [this]()
        {
            FPlayloopEventQueue Queue;
            for (int32 i = 0; i < 8; ++i)
            {
                FPlayloopEvent E;
                E.Id = FString::Printf(TEXT("ID%d"), i);
                E.Name = TEXT("e");
                E.Data = MakeShared<FJsonObject>();
                Queue.Enqueue(E);
            }
            TArray<FPlayloopEvent> Tail = Queue.SnapshotTail(3);
            TestEqual("returns N", Tail.Num(), 3);
            TestEqual("oldest of tail", Tail[0].Id, FString(TEXT("ID5")));
            TestEqual("middle of tail", Tail[1].Id, FString(TEXT("ID6")));
            TestEqual("newest of tail", Tail[2].Id, FString(TEXT("ID7")));
        });
        It("handles a request larger than the buffer", [this]()
        {
            FPlayloopEventQueue Queue;
            FPlayloopEvent E; E.Id = TEXT("ONE"); E.Name = TEXT("e"); E.Data = MakeShared<FJsonObject>();
            Queue.Enqueue(E);
            TArray<FPlayloopEvent> Tail = Queue.SnapshotTail(50);
            TestEqual("returns what we have", Tail.Num(), 1);
        });
    });

    Describe("FPlayloopEventQueue::BuildEnvelopeJson", [this]()
    {
        It("emits the canonical /api/telemetry envelope", [this]()
        {
            FPlayloopEvent A;
            A.Id = TEXT("AAA");
            A.Name = TEXT("session_start");
            A.TimestampMs = 1700000000000;
            A.Data = MakeShared<FJsonObject>();
            A.Data->SetStringField(TEXT("platform"), TEXT("Unreal/Mac"));

            const FString Json = FPlayloopEventQueue::BuildEnvelopeJson(
                { A }, TEXT(""), TEXT("dev-1234"), TEXT("production"),
                nullptr, nullptr, false);

            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
            const bool bOk = FJsonSerializer::Deserialize(Reader, Parsed);
            TestTrue("parsed", bOk && Parsed.IsValid());

            const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
            TestTrue("events array present", Parsed->TryGetArrayField(TEXT("events"), Events));
            TestEqual("event count", Events->Num(), 1);

            TSharedPtr<FJsonObject> First = (*Events)[0]->AsObject();
            TestEqual("event id", First->GetStringField(TEXT("id")), FString(TEXT("AAA")));
            TestEqual("event name", First->GetStringField(TEXT("name")), FString(TEXT("session_start")));

            TestEqual("deviceId on first flush", Parsed->GetStringField(TEXT("deviceId")), FString(TEXT("dev-1234")));
            TestEqual("environment stamped", Parsed->GetStringField(TEXT("environment")), FString(TEXT("production")));
            TestFalse("no sessionEnded marker yet", Parsed->HasField(TEXT("sessionEnded")));
        });

        It("includes sessionEnded:true on the final flush", [this]()
        {
            TSharedPtr<FJsonObject> EndState = MakeShared<FJsonObject>();
            EndState->SetNumberField(TEXT("score"), 42);
            FPlayloopEvent A; A.Id = TEXT("ID"); A.Name = TEXT("session_summary"); A.Data = MakeShared<FJsonObject>();
            const FString Json = FPlayloopEventQueue::BuildEnvelopeJson(
                { A }, TEXT("ps_xyz"), FString(), TEXT("production"),
                nullptr, EndState, true);
            TSharedPtr<FJsonObject> Parsed;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
            FJsonSerializer::Deserialize(Reader, Parsed);
            TestTrue("sessionEnded true", Parsed->GetBoolField(TEXT("sessionEnded")));
            TestEqual("sessionId carried", Parsed->GetStringField(TEXT("sessionId")), FString(TEXT("ps_xyz")));
        });
    });
}

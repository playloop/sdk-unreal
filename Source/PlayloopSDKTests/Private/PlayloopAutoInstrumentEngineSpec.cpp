#include "CoreMinimal.h"
#include "PlayloopTestCompat.h"
#include "Misc/AutomationTest.h"
#include "PlayloopAutoInstrumentEngine.h"

BEGIN_DEFINE_SPEC(FPlayloopAutoInstrumentEngineSpec, "Playloop.AutoInstrument.Engine",
    PLAYLOOP_APP_CONTEXT_MASK | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FPlayloopAutoInstrumentEngineSpec)

void FPlayloopAutoInstrumentEngineSpec::Define()
{
    Describe("FPS rolling median", [this]()
    {
        It("does not fire before the minimum sample count", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            double Now = 0.0;
            // 10 slow frames (5 fps): below 30 sample floor, so no fire.
            for (int32 i = 0; i < 10; ++i)
            {
                Now += 0.2;
                FPlayloopFpsDropDecision D = Engine.RecordFrame(0.2, Now);
                TestFalse("no fire under sample floor", D.bFire);
            }
        });

        It("fires fps_drop once the median is below threshold with enough samples", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            double Now = 0.0;
            bool bFired = false;
            FPlayloopFpsDropDecision Last;
            // 40 frames at ~10 fps (0.1s delta): well under 30 fps threshold.
            for (int32 i = 0; i < 40; ++i)
            {
                Now += 0.1;
                FPlayloopFpsDropDecision D = Engine.RecordFrame(0.1, Now);
                if (D.bFire) { bFired = true; Last = D; }
            }
            TestTrue("fps_drop fired", bFired);
            TestTrue("median below threshold", Last.MedianFps < 30.0);
            TestEqual("windowSec is 3", Last.WindowSec, 3.0);
        });

        It("does not fire when frame rate is healthy", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            double Now = 0.0;
            bool bFired = false;
            // 120 frames at ~60 fps (0.0166s): comfortably above threshold.
            for (int32 i = 0; i < 120; ++i)
            {
                Now += 1.0 / 60.0;
                if (Engine.RecordFrame(1.0 / 60.0, Now).bFire) bFired = true;
            }
            TestFalse("no fps_drop at 60fps", bFired);
        });

        It("throttles to one drop per cooldown window", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            double Now = 0.0;
            int32 FireCount = 0;
            // Sustained 10 fps for ~4 seconds. Cooldown is 5s, so exactly one
            // fire should land in that span.
            for (int32 i = 0; i < 40; ++i)
            {
                Now += 0.1;
                if (Engine.RecordFrame(0.1, Now).bFire) ++FireCount;
            }
            TestEqual("one drop inside the cooldown", FireCount, 1);
        });

        It("ignores non-finite / non-positive deltas", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            FPlayloopFpsDropDecision A = Engine.RecordFrame(0.0, 1.0);
            FPlayloopFpsDropDecision B = Engine.RecordFrame(-0.5, 1.0);
            TestFalse("zero delta ignored", A.bFire);
            TestFalse("negative delta ignored", B.bFire);
        });
    });

    Describe("Idle state machine", [this]()
    {
        It("enters idle after the threshold with no input, then exits on input", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            Engine.ResetIdle(0.0);

            // No input at t=10: not yet idle.
            FPlayloopIdleDecision A = Engine.Tick(false, 10.0);
            TestTrue("not idle before threshold", A.Outcome == FPlayloopIdleDecision::EKind::None);
            TestFalse("engine not idle", Engine.IsIdle());

            // No input at t=31: crosses the 30s threshold → idle_start.
            FPlayloopIdleDecision B = Engine.Tick(false, 31.0);
            TestTrue("idle_start fired", B.Outcome == FPlayloopIdleDecision::EKind::Start);
            TestEqual("idleAfterSec is the threshold", B.IdleAfterSec, 30.0);
            TestTrue("engine now idle", Engine.IsIdle());

            // Input at t=45 → idle_end with the elapsed duration.
            FPlayloopIdleDecision C = Engine.Tick(true, 45.0);
            TestTrue("idle_end fired", C.Outcome == FPlayloopIdleDecision::EKind::End);
            TestEqual("idleDurationSec ~14", C.IdleDurationSec, 14.0);
            TestFalse("engine no longer idle", Engine.IsIdle());
        });

        It("does not re-fire idle_start while already idle", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            Engine.ResetIdle(0.0);
            Engine.Tick(false, 31.0); // start
            FPlayloopIdleDecision Again = Engine.Tick(false, 40.0);
            TestTrue("no second start", Again.Outcome == FPlayloopIdleDecision::EKind::None);
        });
    });

    Describe("Memory cooldown", [this]()
    {
        It("allows one event then suppresses within the cooldown window", [this]()
        {
            FPlayloopAutoInstrumentEngine Engine(30.0, 30.0);
            TestTrue("first memory event allowed", Engine.TryRecordMemoryEvent(0.0));
            TestFalse("second within 30s suppressed", Engine.TryRecordMemoryEvent(10.0));
            TestTrue("allowed again after cooldown", Engine.TryRecordMemoryEvent(31.0));
        });
    });
}

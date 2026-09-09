# Heatmaps, `player_pos` event

Playloop builds per-room heatmaps from telemetry events named exactly
`player_pos` with a payload of `{ x, y, room }`. The SDK has always
supported arbitrary events; this file shows the convention so your data
shows up on the dashboard.

## Minimal example (C++)

Attach this to your player Actor or Character. Tick is gated to ~10Hz
(every 0.1s) so the events are dense enough for clean maps without
inflating the budget.

```cpp
// MyPlayerCharacter.cpp
#include "PlayloopClient.h"
#include "Components/CapsuleComponent.h"

void AMyPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    PrimaryActorTick.bCanEverTick = true;
}

void AMyPlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    StampAccumulator += DeltaTime;
    if (StampAccumulator < 0.1f) return;
    StampAccumulator = 0.0f;

    UPlayloopClient* Pl = UPlayloopClient::GetCurrent();
    if (!Pl) return;

    const FVector Loc = GetActorLocation();
    const FString CurrentRoom = ResolveCurrentRoomTag();  // e.g. "tutorial_01"

    const FString Json = FString::Printf(
        TEXT("{\"x\":%f,\"y\":%f,\"room\":\"%s\"}"),
        Loc.X, Loc.Y, *CurrentRoom);

    Pl->Track(TEXT("player_pos"), Json);
}
```

## Blueprint equivalent

Drop a **Get Current** Playloop Client node and a **Track** node into a
periodic event (a Timer that fires every 0.1s, or a gated Tick). The
`Json Props` pin takes a string literal:

```
{"x":1234.5,"y":-220.0,"room":"act-1-throne"}
```

For cleaner Blueprint authoring, write a small Blueprint Function
Library that formats the payload from a Vector + Name, and call that
from Tick.

## Top-down vs side-scrolling

Unreal's world axes are X forward, Y right, Z up. `x` / `y` are treated
as opaque, so pick whichever two axes are your map plane:

- **Top-down**: pass `Loc.X` and `Loc.Y` straight through.
- **Side-scrolling**: pass `Loc.X` (run direction) and `Loc.Z` (vertical).
- **2D paper plane**: same as top-down, the engine collapses one axis.

The absolute coordinate space does not matter as long as it is stable
within a room.

## Payload rules

- `x` and `y` must be finite numbers (world-space, any unit, cm,
  Unreal units, tiles, etc.).
- `room` must be a non-empty string and stable across sessions for the
  same logical area. `"tutorial_01"` always, never
  `"tutorial_01_v2"` after a re-cook.
- Events missing any field are silently dropped server-side.

## Cadence

Emit a position at ~10 to 20Hz. That's dense enough for clean maps
without inflating your event budget; higher rates mostly add cost.

## Stable room IDs

Use a level package name, sublevel tag, gameplay tag, or any other
identifier that is stable across runs and re-cooks, never a
runtime-generated UUID or a name derived from spawn order.

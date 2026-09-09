# Player Feedback example

Minimum integration for the multi-field Player Feedback surface. The SDK
ships a default UMG form (`UPlayloopFeedbackWidget`) that builds itself at
runtime. It's a dark, rounded modal with a 1–5 rating row, short / long text
fields, yes / no choices, and a Send / Cancel footer, so you can drop it in
with one call and no `.uasset` to maintain.

## Setup

1. Open `/games/<slug>/feedback` in your Playloop dashboard and create a form.
2. Copy the form's `ff_...` id.
3. Make sure a `UPlayloopClient` is running (the SDK auto-starts one when
   **Auto Start** is on in *Project Settings → Plugins → Playloop*; otherwise
   call `Configure()` + `Start()` yourself).

## Default form: one call

The fastest path. `CreateDefault` constructs the widget, adds it to the
viewport, and submits when the player taps **Send**. Pass the field set you
authored in the dashboard so the kinds line up.

```cpp
#include "PlayloopClient.h"
#include "PlayloopFeedbackWidget.h"
#include "PlayloopTypes.h"

void AMyPlayerController::OpenFeedback()
{
    UPlayloopClient* Client = UPlayloopClient::GetCurrent();
    if (!Client)
    {
        return; // SDK not started yet
    }

    TArray<FPlayloopFeedbackField> Fields;

    FPlayloopFeedbackField Rating;
    Rating.Id    = TEXT("q1");
    Rating.Label = TEXT("How was your session?");
    Rating.Kind  = EPlayloopFeedbackFieldKind::Rating1To5;
    Fields.Add(Rating);

    FPlayloopFeedbackField Worked;
    Worked.Id    = TEXT("q2");
    Worked.Label = TEXT("What worked?");
    Worked.Kind  = EPlayloopFeedbackFieldKind::ShortText;
    Fields.Add(Worked);

    FPlayloopFeedbackField Didnt;
    Didnt.Id    = TEXT("q3");
    Didnt.Label = TEXT("What didn't?");
    Didnt.Kind  = EPlayloopFeedbackFieldKind::LongText;
    Fields.Add(Didnt);

    // Use your form id from the dashboard.
    UPlayloopFeedbackWidget* Form = UPlayloopFeedbackWidget::CreateDefault(
        GetWorld(), Client, TEXT("ff_..."), Fields);

    // Optional: react to the result (submitted / dismissed / failed).
    if (Form)
    {
        Form->OnFeedbackResolved.AddDynamic(this, &AMyPlayerController::OnFeedbackDone);
    }
}

void AMyPlayerController::OnFeedbackDone(const FPlayloopFeedbackResult& Result)
{
    if (Result.bOk)
    {
        UE_LOG(LogTemp, Log, TEXT("[Playloop] feedback submitted: %s"), *Result.SubmissionId);
    }
}
```

`OnFeedbackDone` must be a `UFUNCTION()` for the dynamic delegate to bind:

```cpp
UFUNCTION()
void OnFeedbackDone(const FPlayloopFeedbackResult& Result);
```

## Offer it any time, not just at end of run

Wire `OpenFeedback()` to a key and/or a "Feedback" button so motivated
players can reach it whenever they want. A pause-menu entry is the
highest-volume placement. A legacy key bind works alongside Enhanced Input:

```cpp
void AMyPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AMyPlayerController::OpenFeedback);
}
```

Count one submission per run: if a player gives feedback early via the
button, suppress the end-of-run prompt (and vice versa) so it's asked once.

## Field kinds

| Kind | Renders |
| --- | --- |
| `Rating1To5` | a row of 1–5 buttons |
| `ShortText` | a single-line input |
| `LongText` | a multi-line text area |
| `YesNo` | a Yes / No pair |

## Customising the look

Need a pixel-perfect form in your own UI kit? Derive a UMG Blueprint from
`UPlayloopFeedbackWidget`, lay out the rating / yes-no rows in the designer,
and feed values back via `SetRatingValue` / `SetYesNoValue`. The C++ submit
path and `OnFeedbackResolved` delegate are unchanged. Or skip the widget
entirely and call `UPlayloopClient::SubmitFeedback(FormId, Responses, OnDone)`
from your own UI.

Player Feedback is free for every studio. Authoring forms in the dashboard
and runtime submission both stay free.

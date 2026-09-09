#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PlayloopTypes.h"
#include "PlayloopFeedbackWidget.generated.h"

class UPlayloopClient;
class UVerticalBox;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UButton;
class UTextBlock;
class UBorder;
class UPlayloopFeedbackOptionButton;

/**
 * Default Player Feedback widget. Builds the form programmatically in
 * RebuildWidget, so the SDK ships pure C++ with no .uasset to maintain.
 *
 * Three ways to use it:
 *
 * 1. Code path (logic-only): call `UPlayloopClient::SubmitFeedback(...)`
 *    directly from your own UMG widget. Use this when you've already
 *    designed a feedback UI in your project's UI kit.
 *
 * 2. Drop the C++ widget in a Blueprint: derive a new UMG asset
 *    `WBP_PlayloopFeedback` from `UPlayloopFeedbackWidget`. You keep
 *    the C++ logic and override the visual style in the designer.
 *
 * 3. Spawn the default at runtime: call
 *    `UPlayloopFeedbackWidget::CreateDefault(World, Client, FormId,
 *    Fields)` and add it to the viewport. Useful for prototyping or a
 *    "Send feedback" button that doesn't warrant a custom UI.
 *
 * Player Feedback is free for every studio. Authoring forms in the
 * dashboard AND the runtime submission both stay free.
 */
UCLASS(BlueprintType, Blueprintable)
class PLAYLOOPSDK_API UPlayloopFeedbackWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPlayloopFeedbackWidget(const FObjectInitializer& ObjectInitializer);

    /**
     * Construct, configure, and add a default feedback widget to the
     * viewport. Caller owns the returned pointer (the widget removes
     * itself on submit / dismiss).
     *
     * Returns nullptr if `Client` is null or `FormId` is empty.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Feedback", meta = (WorldContext = "WorldContext"))
    static UPlayloopFeedbackWidget* CreateDefault(
        UObject* WorldContext,
        UPlayloopClient* Client,
        const FString& FormId,
        const TArray<FPlayloopFeedbackField>& Fields);

    /**
     * True while a default feedback form is on screen. CreateDefault
     * returns nullptr while this holds, so a second prompt can't stack on
     * the first (matches the Unity/Godot single-overlay guard).
     */
    UFUNCTION(BlueprintPure, Category = "Playloop|Feedback")
    static bool IsAnyOpen();

    /**
     * Warm the "Powered by Playloop" brand-mark texture off the game thread
     * so the first feedback-form open doesn't pay a synchronous disk read +
     * decode mid-gameplay. Idempotent and safe to call before any form shows
     * (the SDK calls it from UPlayloopClient::Start()). No-op once cached.
     */
    static void PreloadBranding();

    // UUserWidget declares a virtual bool Initialize(); our 3-arg overload
    // below would hide it. Pull the base into scope so UE 5.8's
    // -Woverloaded-virtual (now -Werror) doesn't fire on the shadow.
    using UUserWidget::Initialize;

    /** Configure before AddToViewport. Mandatory if you constructed the widget by hand. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Feedback")
    void Initialize(
        UPlayloopClient* InClient,
        const FString& InFormId,
        const TArray<FPlayloopFeedbackField>& InFields);

    /** Fires after Submit succeeds, fails, or the player dismisses the form. */
    UPROPERTY(BlueprintAssignable, Category = "Playloop|Feedback")
    FOnPlayloopFeedbackSubmitted OnFeedbackResolved;

    /**
     * Set a rating value for one of the form's rating-1-5 fields. Wire
     * this from your Blueprint subclass's rating widget (5 buttons,
     * each setting `Value = 1..5`) so the C++ submit path picks the
     * value up. `FieldIndex` is the position of the field in the
     * `InFields` array passed to Initialize.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Feedback")
    void SetRatingValue(int32 FieldIndex, int32 Value);

    /**
     * Set a yes/no value for one of the form's yes-no fields. Pass
     * `"yes"` or `"no"`; any other value is treated as empty.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Feedback")
    void SetYesNoValue(int32 FieldIndex, const FString& Value);

    /**
     * Called by an option button (rating star / yes-no) when clicked.
     * Records the value and re-highlights that field's buttons so the
     * selection is visible. Internal: wired up in RebuildWidget.
     */
    void OnOptionSelected(UPlayloopFeedbackOptionButton* Btn);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    // Claim/release the single-overlay guard and take keyboard focus so
    // Escape dismisses the form (parity with the Unity/Godot widgets).
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    UFUNCTION()
    void HandleSendClicked();

    UFUNCTION()
    void HandleCancelClicked();

    // Opens https://playloop.gg when the "Powered by Playloop" badge is
    // clicked. Only wired when the server-resolved branding flag is on.
    UFUNCTION()
    void HandleBrandingClicked();

    // Relays the single-cast SubmitFeedback result onto the multicast
    // BlueprintAssignable OnFeedbackResolved event. (SubmitFeedback takes a
    // single-cast delegate, UHT's rule for BlueprintCallable params, which
    // can only bind a UFUNCTION, so this can't be an inline lambda.)
    UFUNCTION()
    void HandleSubmitResult(const FPlayloopFeedbackResult& Result);

    UPROPERTY()
    TObjectPtr<UPlayloopClient> Client = nullptr;

    UPROPERTY()
    FString FormId;

    UPROPERTY()
    TArray<FPlayloopFeedbackField> Fields;

    // Runtime state per field. RatingValues / YesNoValues are kept
    // separate from the text widgets so we can read each kind back
    // without re-discovering the slate hierarchy.
    UPROPERTY()
    TArray<int32> RatingValues;

    UPROPERTY()
    TArray<FString> YesNoValues;

    UPROPERTY()
    TArray<TObjectPtr<UEditableTextBox>> ShortTextInputs;

    UPROPERTY()
    TArray<TObjectPtr<UMultiLineEditableTextBox>> LongTextInputs;

    // Index map: for each field in Fields, the entry in the matching
    // per-kind array above. -1 when the field uses a different kind.
    UPROPERTY()
    TArray<int32> FieldKindIndex;

    // Rating + yes-no option buttons, kept so OnOptionSelected can
    // re-highlight the chosen one within a field.
    UPROPERTY()
    TArray<TObjectPtr<UPlayloopFeedbackOptionButton>> OptionButtons;

    // Validation message shown when Send is blocked by an unanswered required
    // field. Collapsed until then (parity with the Unity/Godot widgets, which
    // flag the missing field so the player knows why Send did nothing).
    UPROPERTY()
    TObjectPtr<UTextBlock> ErrorText = nullptr;

    int64 AskedAtSec = 0;
};

/**
 * A single tappable option in the default feedback form: one star of a
 * rating row, or a Yes / No choice. Carries which field it belongs to and
 * the value it sets so the C++ submit path can read the selection back
 * without a Blueprint. Internal to the default widget.
 */
UCLASS()
class UPlayloopFeedbackOptionButton final : public UButton
{
    GENERATED_BODY()

public:
    /** Index of the owning field in the form's Fields array. */
    UPROPERTY()
    int32 FieldIndex = -1;

    /** Value for a Rating1To5 field (1..5). 0 for yes-no. */
    UPROPERTY()
    int32 IntValue = 0;

    /** Value for a YesNo field ("yes" / "no"). Empty for ratings. */
    UPROPERTY()
    FString StrValue;

    /** The form this button belongs to. */
    UPROPERTY()
    TObjectPtr<UPlayloopFeedbackWidget> Owner;

    /** Bound to this button's OnClicked; forwards to the owning form. */
    UFUNCTION()
    void HandleClicked();
};

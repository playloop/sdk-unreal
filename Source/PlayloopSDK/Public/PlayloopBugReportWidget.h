#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PlayloopTypes.h"
#include "PlayloopBugReportWidget.generated.h"

class UPlayloopClient;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UTextBlock;
class UPlayloopBugSeverityButton;

/**
 * Default Player Bug Report widget. Builds the form programmatically in
 * RebuildWidget, so the SDK ships pure C++ with no .uasset to maintain. Mirrors
 * UPlayloopFeedbackWidget in structure.
 *
 * Fields: a single-line Title, a multi-line Description, a four-option Severity
 * selector (default Medium), plus Submit + Cancel.
 *
 * Three ways to use it:
 *
 * 1. Code path (logic-only): call `UPlayloopClient::SubmitBugReport(...)`
 *    directly from your own UMG widget. Use this when you've already designed a
 *    bug-report UI in your project's UI kit.
 *
 * 2. Drop the C++ widget in a Blueprint: derive `WBP_PlayloopBugReport` from
 *    `UPlayloopBugReportWidget`. You keep the C++ logic and override the visual
 *    style in the designer.
 *
 * 3. Spawn the default at runtime: call
 *    `UPlayloopBugReportWidget::CreateDefault(World, Client)` and add it to the
 *    viewport. Useful for a "Report a bug" button that doesn't warrant a custom
 *    UI.
 *
 * Player Bug Reports are free for every studio.
 */
UCLASS(BlueprintType, Blueprintable)
class PLAYLOOPSDK_API UPlayloopBugReportWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPlayloopBugReportWidget(const FObjectInitializer& ObjectInitializer);

    /**
     * Construct, configure, and add a default bug-report widget to the
     * viewport. Caller owns the returned pointer (the widget removes itself on
     * submit / dismiss). Returns nullptr if `Client` is null or a form is
     * already open.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|BugReports", meta = (WorldContext = "WorldContext"))
    static UPlayloopBugReportWidget* CreateDefault(UObject* WorldContext, UPlayloopClient* Client);

    /**
     * True while a default bug-report form is on screen. CreateDefault returns
     * nullptr while this holds, so a second prompt can't stack on the first.
     */
    UFUNCTION(BlueprintPure, Category = "Playloop|BugReports")
    static bool IsAnyOpen();

    // UUserWidget declares a virtual bool Initialize(); our overload below would
    // hide it. Pull the base into scope so -Woverloaded-virtual doesn't fire.
    using UUserWidget::Initialize;

    /** Configure before AddToViewport. Mandatory if you constructed the widget by hand. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|BugReports")
    void Initialize(UPlayloopClient* InClient);

    /** Fires after Submit succeeds, fails, or the player dismisses the form. */
    UPROPERTY(BlueprintAssignable, Category = "Playloop|BugReports")
    FOnPlayloopBugReportSubmitted OnBugReportResolved;

    /** Set the selected severity. Wired from the built-in selector; also callable from a Blueprint subclass. */
    UFUNCTION(BlueprintCallable, Category = "Playloop|BugReports")
    void SetSeverity(EPlayloopBugSeverity InSeverity);

    /** Internal: called by a severity option button when clicked. */
    void OnSeveritySelected(UPlayloopBugSeverityButton* Btn);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

    UFUNCTION()
    void HandleSendClicked();

    UFUNCTION()
    void HandleCancelClicked();

    UFUNCTION()
    void HandleBrandingClicked();

    // Relays the single-cast SubmitBugReport result onto the multicast
    // BlueprintAssignable OnBugReportResolved event.
    UFUNCTION()
    void HandleSubmitResult(const FPlayloopBugReportResult& Result);

    UPROPERTY()
    TObjectPtr<UPlayloopClient> Client = nullptr;

    UPROPERTY()
    TObjectPtr<UEditableTextBox> TitleInput = nullptr;

    UPROPERTY()
    TObjectPtr<UMultiLineEditableTextBox> DescriptionInput = nullptr;

    UPROPERTY()
    EPlayloopBugSeverity Severity = EPlayloopBugSeverity::Medium;

    UPROPERTY()
    TArray<TObjectPtr<UPlayloopBugSeverityButton>> SeverityButtons;

    // Validation message shown when Send is blocked by an empty title /
    // description. Collapsed until then.
    UPROPERTY()
    TObjectPtr<UTextBlock> ErrorText = nullptr;
};

/**
 * A single tappable severity option in the default bug-report form. Carries the
 * severity value it sets so the C++ submit path can read the selection back
 * without a Blueprint. Internal to the default widget.
 */
UCLASS()
class UPlayloopBugSeverityButton final : public UButton
{
    GENERATED_BODY()

public:
    /** The severity this button selects. */
    UPROPERTY()
    EPlayloopBugSeverity Value = EPlayloopBugSeverity::Medium;

    /** The form this button belongs to. */
    UPROPERTY()
    TObjectPtr<UPlayloopBugReportWidget> Owner;

    /** Bound to this button's OnClicked; forwards to the owning form. */
    UFUNCTION()
    void HandleClicked();
};

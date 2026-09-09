#include "PlayloopBugReportWidget.h"

#include "PlayloopClient.h"
#include "PlayloopEventConfigStore.h"

#include "HAL/PlatformProcess.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "InputCoreTypes.h"

namespace
{
    // Palette + helpers mirror the Player Feedback form so the two default
    // widgets read as one family. Colours are 8-bit sRGB (matched to the Godot/
    // Unity forms) converted to linear by UE.
    const FLinearColor kCardColor       = FLinearColor(FColor(23, 26, 31));
    const FLinearColor kCardBorder      = FLinearColor(FColor(51, 54, 61));
    const FLinearColor kInputBg         = FLinearColor(FColor(33, 36, 41));
    const FLinearColor kInputBorder     = FLinearColor(FColor(46, 49, 57));
    const FLinearColor kAccent          = FLinearColor(FColor(102, 158, 255));
    const FLinearColor kAccentHover     = FLinearColor(FColor(122, 176, 255));
    const FLinearColor kButtonSecondary = FLinearColor(FColor(41, 43, 51));
    const FLinearColor kButtonHover     = FLinearColor(FColor(54, 57, 66));
    const FLinearColor kLabelColor      = FLinearColor(FColor(217, 217, 224));
    const FLinearColor kMutedColor      = FLinearColor(FColor(124, 128, 140));
    const FLinearColor kErrorColor      = FLinearColor(FColor(239, 118, 122));

    constexpr float kRadius = 6.f;

    FSlateFontInfo BoldFont(int32 Size)    { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }
    FSlateFontInfo RegularFont(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }

    void StyleFilledButton(UButton* Button, const FLinearColor& Bg, const FLinearColor& Hover,
                           const FLinearColor& Border = FLinearColor(0, 0, 0, 0), float BorderW = 0.f,
                           const FMargin& Pad = FMargin(14.f, 9.f))
    {
        if (!Button) return;
        FButtonStyle Style = Button->GetStyle();
        Style.SetNormal(FSlateRoundedBoxBrush(Bg, kRadius, Border, BorderW));
        Style.SetHovered(FSlateRoundedBoxBrush(Hover, kRadius, Border, BorderW));
        Style.SetPressed(FSlateRoundedBoxBrush(Bg, kRadius, Border, BorderW));
        Style.SetNormalPadding(Pad);
        Style.SetPressedPadding(Pad);
        Button->SetStyle(Style);
    }

    FEditableTextBoxStyle InputStyle()
    {
        FEditableTextBoxStyle S;
        S.SetBackgroundImageNormal(FSlateRoundedBoxBrush(kInputBg, kRadius, kInputBorder, 1.f));
        S.SetBackgroundImageHovered(FSlateRoundedBoxBrush(kInputBg, kRadius, kAccent, 1.f));
        S.SetBackgroundImageFocused(FSlateRoundedBoxBrush(kInputBg, kRadius, kAccent, 1.f));
        S.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(kInputBg, kRadius, kInputBorder, 1.f));
        S.SetForegroundColor(FSlateColor(kLabelColor));
        S.SetBackgroundColor(FSlateColor(kInputBg));
        S.SetFont(RegularFont(12));
        S.SetPadding(FMargin(11.f, 9.f));
        return S;
    }

    // Single-overlay guard: the default bug-report widget currently on screen,
    // if any. Independent of the feedback widget's guard (a game may show one of
    // each), but never two bug-report forms.
    TWeakObjectPtr<UPlayloopBugReportWidget> GOpenBugReportWidget;
}

UPlayloopBugReportWidget::UPlayloopBugReportWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

UPlayloopBugReportWidget* UPlayloopBugReportWidget::CreateDefault(UObject* WorldContext, UPlayloopClient* InClient)
{
    if (InClient == nullptr) return nullptr;
    if (GOpenBugReportWidget.IsValid()) return nullptr;
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
    if (World == nullptr) return nullptr;

    UPlayloopBugReportWidget* Widget = CreateWidget<UPlayloopBugReportWidget>(World, UPlayloopBugReportWidget::StaticClass());
    if (Widget == nullptr) return nullptr;
    Widget->Initialize(InClient);
    Widget->AddToViewport(/*ZOrder=*/ 32000);
    return Widget;
}

void UPlayloopBugReportWidget::Initialize(UPlayloopClient* InClient)
{
    Client = InClient;
    Severity = EPlayloopBugSeverity::Medium;
}

bool UPlayloopBugReportWidget::IsAnyOpen()
{
    return GOpenBugReportWidget.IsValid();
}

void UPlayloopBugReportWidget::NativeConstruct()
{
    Super::NativeConstruct();
    GOpenBugReportWidget = this;
    SetIsFocusable(true);
    SetKeyboardFocus();
}

void UPlayloopBugReportWidget::NativeDestruct()
{
    if (GOpenBugReportWidget.Get() == this)
    {
        GOpenBugReportWidget.Reset();
    }
    Super::NativeDestruct();
}

FReply UPlayloopBugReportWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        HandleCancelClicked();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

TSharedRef<SWidget> UPlayloopBugReportWidget::RebuildWidget()
{
    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrush(FSlateRoundedBoxBrush(kCardColor, 12.f, kCardBorder, 1.f));
    Panel->SetPadding(FMargin(26.f, 26.f));

    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Stack"));
    Panel->SetContent(Stack);

    // Title row: "Report a bug" with a × close pushed to the right edge.
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    UTextBlock* Heading = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Heading"));
    Heading->SetText(FText::FromString(TEXT("Report a bug")));
    Heading->SetFont(BoldFont(20));
    Heading->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    if (UHorizontalBoxSlot* HeadingSlot = TitleRow->AddChildToHorizontalBox(Heading))
    {
        HeadingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        HeadingSlot->SetVerticalAlignment(VAlign_Center);
    }
    UButton* Close = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Close, FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.06f),
                      FLinearColor(0, 0, 0, 0), 0.f, FMargin(7.f, 1.f));
    UTextBlock* CloseX = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    CloseX->SetText(FText::FromString(TEXT("×")));
    CloseX->SetFont(BoldFont(18));
    CloseX->SetColorAndOpacity(FSlateColor(kMutedColor));
    Close->AddChild(CloseX);
    Close->OnClicked.AddDynamic(this, &UPlayloopBugReportWidget::HandleCancelClicked);
    if (UHorizontalBoxSlot* CloseSlot = TitleRow->AddChildToHorizontalBox(Close))
    {
        CloseSlot->SetVerticalAlignment(VAlign_Center);
    }
    if (UVerticalBoxSlot* TitleSlot = Stack->AddChildToVerticalBox(TitleRow))
    {
        TitleSlot->SetPadding(FMargin(0, 0, 0, 16));
    }

    // Title field (single line).
    {
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(TEXT("Title")));
        Label->SetFont(BoldFont(14));
        Label->SetColorAndOpacity(FSlateColor(kLabelColor));
        if (UVerticalBoxSlot* LabelSlot = Stack->AddChildToVerticalBox(Label))
        {
            LabelSlot->SetPadding(FMargin(0, 0, 0, 7));
        }
        TitleInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
        TitleInput->WidgetStyle = InputStyle();
        TitleInput->SetHintText(FText::FromString(TEXT("Short summary of the problem")));
        Stack->AddChildToVerticalBox(TitleInput);
    }

    // Description field (multi line).
    {
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(TEXT("What happened?")));
        Label->SetFont(BoldFont(14));
        Label->SetColorAndOpacity(FSlateColor(kLabelColor));
        if (UVerticalBoxSlot* LabelSlot = Stack->AddChildToVerticalBox(Label))
        {
            LabelSlot->SetPadding(FMargin(0, 16, 0, 7));
        }
        DescriptionInput = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass());
        DescriptionInput->WidgetStyle = InputStyle();
        DescriptionInput->SetHintText(FText::FromString(TEXT("Steps to reproduce, what you expected, what you saw...")));
        USizeBox* Tall = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Tall->SetHeightOverride(120.f);
        Tall->SetContent(DescriptionInput);
        Stack->AddChildToVerticalBox(Tall);
    }

    // Severity selector: four options, Medium selected by default.
    {
        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(TEXT("Severity")));
        Label->SetFont(BoldFont(14));
        Label->SetColorAndOpacity(FSlateColor(kLabelColor));
        if (UVerticalBoxSlot* LabelSlot = Stack->AddChildToVerticalBox(Label))
        {
            LabelSlot->SetPadding(FMargin(0, 16, 0, 7));
        }

        SeverityButtons.Reset();
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        const TCHAR* Labels[4]         = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Critical") };
        const EPlayloopBugSeverity Vals[4] = { EPlayloopBugSeverity::Low, EPlayloopBugSeverity::Medium, EPlayloopBugSeverity::High, EPlayloopBugSeverity::Critical };
        for (int32 i = 0; i < 4; ++i)
        {
            UPlayloopBugSeverityButton* Btn = WidgetTree->ConstructWidget<UPlayloopBugSeverityButton>(UPlayloopBugSeverityButton::StaticClass());
            Btn->Value = Vals[i];
            Btn->Owner = this;
            const bool bSelected = (Vals[i] == Severity);
            StyleFilledButton(Btn,
                              bSelected ? kAccent : kButtonSecondary,
                              bSelected ? kAccentHover : kButtonHover,
                              bSelected ? FLinearColor(0, 0, 0, 0) : kInputBorder,
                              bSelected ? 0.f : 1.f,
                              FMargin(0.f, 11.f));
            UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            T->SetText(FText::FromString(Labels[i]));
            T->SetFont(BoldFont(13));
            T->SetColorAndOpacity(FSlateColor(bSelected ? FLinearColor::White : kLabelColor));
            Btn->AddChild(T);
            Btn->OnClicked.AddDynamic(Btn, &UPlayloopBugSeverityButton::HandleClicked);
            if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Btn))
            {
                S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                S->SetPadding(FMargin(i == 0 ? 0.f : 8.f, 0, 0, 0));
            }
            SeverityButtons.Add(Btn);
        }
        Stack->AddChildToVerticalBox(Row);
    }

    // Cancel + Send row.
    UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

    UButton* Cancel = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Cancel, kButtonSecondary, kButtonHover, kInputBorder, 1.f, FMargin(0.f, 11.f));
    UTextBlock* CancelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    CancelText->SetText(FText::FromString(TEXT("Cancel")));
    CancelText->SetFont(BoldFont(14));
    CancelText->SetColorAndOpacity(FSlateColor(kLabelColor));
    Cancel->AddChild(CancelText);
    Cancel->OnClicked.AddDynamic(this, &UPlayloopBugReportWidget::HandleCancelClicked);
    if (UHorizontalBoxSlot* CancelSlot = ButtonRow->AddChildToHorizontalBox(Cancel))
    {
        CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CancelSlot->SetPadding(FMargin(0, 0, 10, 0));
    }

    UButton* Send = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Send, kAccent, kAccentHover, FLinearColor(0, 0, 0, 0), 0.f, FMargin(0.f, 11.f));
    UTextBlock* SendText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    SendText->SetText(FText::FromString(TEXT("Send report")));
    SendText->SetFont(BoldFont(14));
    SendText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Send->AddChild(SendText);
    Send->OnClicked.AddDynamic(this, &UPlayloopBugReportWidget::HandleSendClicked);
    if (UHorizontalBoxSlot* SendSlot = ButtonRow->AddChildToHorizontalBox(Send))
    {
        FSlateChildSize Wider(ESlateSizeRule::Fill);
        Wider.Value = 1.4f;
        SendSlot->SetSize(Wider);
    }

    // Required-field validation message, above the buttons. Hidden until Send is
    // blocked by an empty title / description (see HandleSendClicked).
    UTextBlock* Err = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Err->SetText(FText::FromString(TEXT("Please add a title and a description before sending.")));
    Err->SetFont(RegularFont(12));
    Err->SetColorAndOpacity(FSlateColor(kErrorColor));
    Err->SetVisibility(ESlateVisibility::Collapsed);
    ErrorText = Err;
    if (UVerticalBoxSlot* ErrSlot = Stack->AddChildToVerticalBox(Err))
    {
        ErrSlot->SetPadding(FMargin(0, 16, 0, 0));
    }

    if (UVerticalBoxSlot* ButtonRowSlot = Stack->AddChildToVerticalBox(ButtonRow))
    {
        ButtonRowSlot->SetPadding(FMargin(0, 22, 0, 0));
    }

    // Footer: clickable "Powered by Playloop" text, centered. Gated on the
    // server-resolved branding flag (paid studios can turn it off; free studios
    // always show it, default true before the event-config fetch lands).
    const bool bShowBranding = Client ? Client->GetGameSettings().bShowBranding : true;
    if (bShowBranding)
    {
        UButton* FooterBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        FButtonStyle FooterStyle = FooterBtn->GetStyle();
        const FSlateRoundedBoxBrush Transparent(FLinearColor(0, 0, 0, 0), 0.f);
        FooterStyle.SetNormal(Transparent);
        FooterStyle.SetHovered(Transparent);
        FooterStyle.SetPressed(Transparent);
        FooterStyle.SetNormalPadding(FMargin(0.f));
        FooterStyle.SetPressedPadding(FMargin(0.f));
        FooterBtn->SetStyle(FooterStyle);
        FooterBtn->SetCursor(EMouseCursor::Hand);
        FooterBtn->OnClicked.AddDynamic(this, &UPlayloopBugReportWidget::HandleBrandingClicked);

        UTextBlock* Footer = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Footer->SetText(FText::FromString(TEXT("Powered by Playloop")));
        Footer->SetFont(RegularFont(12));
        Footer->SetColorAndOpacity(FSlateColor(kMutedColor));
        FooterBtn->AddChild(Footer);

        if (UVerticalBoxSlot* FooterSlot = Stack->AddChildToVerticalBox(FooterBtn))
        {
            FooterSlot->SetPadding(FMargin(0, 14, 0, 0));
            FooterSlot->SetHorizontalAlignment(HAlign_Center);
        }
    }

    // Center the card on a dimmed full-screen backdrop so it reads as a modal.
    USizeBox* CardSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSizer"));
    float CardWidth = 560.f;
    if (GEngine && GEngine->GameViewport)
    {
        FVector2D ViewportSize;
        GEngine->GameViewport->GetViewportSize(ViewportSize);
        if (ViewportSize.X > 0.f)
        {
            CardWidth = FMath::Clamp(static_cast<float>(ViewportSize.X) - 48.f, 280.f, 560.f);
        }
    }
    CardSizer->SetWidthOverride(CardWidth);
    CardSizer->SetContent(Panel);

    UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
    Backdrop->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
    Backdrop->SetHorizontalAlignment(HAlign_Center);
    Backdrop->SetVerticalAlignment(VAlign_Center);
    Backdrop->SetContent(CardSizer);

    WidgetTree->RootWidget = Backdrop;
    return Super::RebuildWidget();
}

void UPlayloopBugReportWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    TitleInput = nullptr;
    DescriptionInput = nullptr;
}

void UPlayloopBugReportWidget::SetSeverity(EPlayloopBugSeverity InSeverity)
{
    Severity = InSeverity;

    // Re-highlight: the chosen button gets the accent fill, its siblings fall
    // back to the secondary fill.
    for (UPlayloopBugSeverityButton* B : SeverityButtons)
    {
        if (!B) continue;
        const bool bSelected = (B->Value == Severity);
        StyleFilledButton(B,
                          bSelected ? kAccent : kButtonSecondary,
                          bSelected ? kAccentHover : kButtonHover,
                          bSelected ? FLinearColor(0, 0, 0, 0) : kInputBorder,
                          bSelected ? 0.f : 1.f,
                          FMargin(0.f, 11.f));
    }
}

void UPlayloopBugReportWidget::OnSeveritySelected(UPlayloopBugSeverityButton* Btn)
{
    if (!Btn) return;
    SetSeverity(Btn->Value);
}

void UPlayloopBugSeverityButton::HandleClicked()
{
    if (Owner)
    {
        Owner->OnSeveritySelected(this);
    }
}

void UPlayloopBugReportWidget::HandleCancelClicked()
{
    FPlayloopBugReportResult Result;
    Result.bOk = false;
    Result.ErrorCode = TEXT("dismissed");
    OnBugReportResolved.Broadcast(Result);
    RemoveFromParent();
}

void UPlayloopBugReportWidget::HandleBrandingClicked()
{
    FPlatformProcess::LaunchURL(TEXT("https://playloop.gg"), nullptr, nullptr);
}

void UPlayloopBugReportWidget::HandleSendClicked()
{
    if (Client == nullptr) return;

    if (ErrorText) ErrorText->SetVisibility(ESlateVisibility::Collapsed);

    const FString Title = TitleInput ? TitleInput->GetText().ToString().TrimStartAndEnd() : FString();
    const FString Description = DescriptionInput ? DescriptionInput->GetText().ToString().TrimStartAndEnd() : FString();

    if (Title.IsEmpty() || Description.IsEmpty())
    {
        if (ErrorText) ErrorText->SetVisibility(ESlateVisibility::Visible);
        return;
    }

    FOnPlayloopBugReportSubmittedBP Forward;
    Forward.BindDynamic(this, &UPlayloopBugReportWidget::HandleSubmitResult);
    Client->SubmitBugReport(Title, Description, Severity, Forward);
    RemoveFromParent();
}

void UPlayloopBugReportWidget::HandleSubmitResult(const FPlayloopBugReportResult& Result)
{
    OnBugReportResolved.Broadcast(Result);
}

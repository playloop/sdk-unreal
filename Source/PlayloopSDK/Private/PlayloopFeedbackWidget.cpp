#include "PlayloopFeedbackWidget.h"

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
// UE 5.8 IWYU: UTexture2D is used as a complete type below (member access +
// TWeakObjectPtr), no longer transitively included via ImageUtils/Engine.
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Components/Image.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"

namespace
{
    // Palette taken verbatim from the canonical Godot/Unity feedback form.
    // Those SDKs specify colours in sRGB (Godot Color / Unity Color), so we
    // build each via FColor (8-bit sRGB) and let UE convert to linear. Using
    // the raw floats as FLinearColor would render everything too bright.
    const FLinearColor kCardColor       = FLinearColor(FColor(23, 26, 31));   // 0.09,0.10,0.12
    const FLinearColor kCardBorder      = FLinearColor(FColor(51, 54, 61));   // 0.20,0.21,0.24
    const FLinearColor kInputBg         = FLinearColor(FColor(33, 36, 41));   // 0.13,0.14,0.16
    const FLinearColor kInputBorder     = FLinearColor(FColor(46, 49, 57));
    const FLinearColor kAccent          = FLinearColor(FColor(102, 158, 255)); // 0.40,0.62,1.0
    const FLinearColor kAccentHover     = FLinearColor(FColor(122, 176, 255));
    const FLinearColor kButtonSecondary = FLinearColor(FColor(41, 43, 51));   // 0.16,0.17,0.20
    const FLinearColor kButtonHover     = FLinearColor(FColor(54, 57, 66));
    const FLinearColor kLabelColor      = FLinearColor(FColor(217, 217, 224)); // 0.85,0.85,0.88
    const FLinearColor kMutedColor      = FLinearColor(FColor(124, 128, 140));
    const FLinearColor kErrorColor      = FLinearColor(FColor(239, 118, 122));  // soft red for required-field validation

    constexpr float kRadius = 6.f;

    FSlateFontInfo BoldFont(int32 Size)    { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }
    FSlateFontInfo RegularFont(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }

    // A filled, rounded button with a hover lift. radius/border optional.
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

    // Dark, rounded text-input styling for single- and multi-line boxes.
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

    // The Playloop mark shipped in the plugin's Resources/, loaded
    // asynchronously and cached (rooted so it survives GC, decoded once).
    //
    // The expensive part. the synchronous disk read. runs on a worker
    // thread; the tiny 48x48 decode + UTexture2D creation finalize on the game
    // thread (UObject creation is game-thread-only). This keeps the first
    // feedback-form open from micro-hitching the game thread mid-gameplay.
    // All cache state is touched ONLY on the game thread.
    enum class EBrandMarkState : uint8 { NotStarted, Loading, Ready, Failed };
    EBrandMarkState GBrandMarkState = EBrandMarkState::NotStarted;
    TWeakObjectPtr<UTexture2D> GBrandMark;

    FString BrandMarkPath()
    {
        TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlayloopSDK"));
        if (!Plugin.IsValid()) return FString();
        return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("playloop-mark.png"));
    }

    // Kicks the async load once. Must be called on the game thread.
    void EnsureBrandMarkLoading()
    {
        if (GBrandMarkState != EBrandMarkState::NotStarted) return;
        const FString Path = BrandMarkPath();
        if (Path.IsEmpty() || !FPaths::FileExists(Path))
        {
            GBrandMarkState = EBrandMarkState::Failed;
            return;
        }
        GBrandMarkState = EBrandMarkState::Loading;
        Async(EAsyncExecution::ThreadPool, [Path]()
        {
            TArray<uint8> Bytes;
            const bool bRead = FFileHelper::LoadFileToArray(Bytes, *Path);
            AsyncTask(ENamedThreads::GameThread, [bRead, Bytes = MoveTemp(Bytes)]()
            {
                UTexture2D* Tex = bRead ? FImageUtils::ImportBufferAsTexture2D(Bytes) : nullptr;
                if (Tex)
                {
                    Tex->AddToRoot();
                    GBrandMark = Tex;
                    GBrandMarkState = EBrandMarkState::Ready;
                }
                else
                {
                    GBrandMarkState = EBrandMarkState::Failed;
                }
            });
        });
    }

    // Returns the cached mark if the async load has finished; otherwise kicks
    // the load and returns nullptr (the footer text still renders, just without
    // the logo this open. it appears on the next). Never blocks the game thread.
    UTexture2D* GetBrandMarkIfReady()
    {
        if (GBrandMarkState == EBrandMarkState::Ready && GBrandMark.IsValid())
        {
            return GBrandMark.Get();
        }
        EnsureBrandMarkLoading();
        return nullptr;
    }

    // Single-overlay guard: the default feedback widget currently on screen,
    // if any. Set when one is constructed into the viewport, cleared when it
    // leaves (NativeDestruct). CreateDefault refuses to stack a second.
    TWeakObjectPtr<UPlayloopFeedbackWidget> GOpenFeedbackWidget;
}

UPlayloopFeedbackWidget::UPlayloopFeedbackWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

UPlayloopFeedbackWidget* UPlayloopFeedbackWidget::CreateDefault(
    UObject* WorldContext,
    UPlayloopClient* InClient,
    const FString& InFormId,
    const TArray<FPlayloopFeedbackField>& InFields)
{
    if (InClient == nullptr || InFormId.IsEmpty()) return nullptr;
    // Single-overlay guard: never stack a second prompt on the first
    // (parity with the Unity/Godot widgets). The open form clears the
    // guard when it leaves the viewport.
    if (GOpenFeedbackWidget.IsValid()) return nullptr;
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
    if (World == nullptr) return nullptr;

    UPlayloopFeedbackWidget* Widget = CreateWidget<UPlayloopFeedbackWidget>(World, UPlayloopFeedbackWidget::StaticClass());
    if (Widget == nullptr) return nullptr;
    Widget->Initialize(InClient, InFormId, InFields);
    Widget->AddToViewport(/*ZOrder=*/ 32000);
    return Widget;
}

void UPlayloopFeedbackWidget::Initialize(
    UPlayloopClient* InClient,
    const FString& InFormId,
    const TArray<FPlayloopFeedbackField>& InFields)
{
    Client = InClient;
    FormId = InFormId;
    Fields = InFields;
    AskedAtSec = FDateTime::UtcNow().ToUnixTimestamp();
    RatingValues.Init(0, Fields.Num());
    YesNoValues.Init(FString(), Fields.Num());
    FieldKindIndex.Init(-1, Fields.Num());
    ShortTextInputs.Reset();
    LongTextInputs.Reset();
}

bool UPlayloopFeedbackWidget::IsAnyOpen()
{
    return GOpenFeedbackWidget.IsValid();
}

void UPlayloopFeedbackWidget::PreloadBranding()
{
    // Cache state is game-thread-only; hop there if called from elsewhere.
    if (IsInGameThread())
    {
        EnsureBrandMarkLoading();
    }
    else
    {
        AsyncTask(ENamedThreads::GameThread, []() { EnsureBrandMarkLoading(); });
    }
}

void UPlayloopFeedbackWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // Claim the single-overlay guard and take keyboard focus so Escape
    // reaches NativeOnKeyDown even before the player clicks a field.
    GOpenFeedbackWidget = this;
    SetIsFocusable(true);
    SetKeyboardFocus();
}

void UPlayloopFeedbackWidget::NativeDestruct()
{
    if (GOpenFeedbackWidget.Get() == this)
    {
        GOpenFeedbackWidget.Reset();
    }
    Super::NativeDestruct();
}

FReply UPlayloopFeedbackWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        HandleCancelClicked();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

TSharedRef<SWidget> UPlayloopFeedbackWidget::RebuildWidget()
{
    // Programmatic UMG construction. Fields with kinds that the
    // default widget renders directly (short-text / long-text) land
    // in their respective UPROPERTY arrays so HandleSendClicked can
    // read them back. Rating + yes-no fields render as labeled
    // placeholders here; studios that want a pixel-perfect default
    // for those kinds derive a Blueprint from this class and lay them
    // out in the UMG designer (the OnFeedbackResolved delegate +
    // SubmitFeedback dispatch are unchanged).
    if (Fields.Num() == 0)
    {
        return Super::RebuildWidget();
    }

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    Panel->SetBrush(FSlateRoundedBoxBrush(kCardColor, 12.f, kCardBorder, 1.f));
    Panel->SetPadding(FMargin(26.f, 26.f));

    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Stack"));
    Panel->SetContent(Stack);

    // Title row: "Feedback" with a × close pushed to the right edge.
    UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
    Title->SetText(FText::FromString(TEXT("Feedback")));
    Title->SetFont(BoldFont(20));
    Title->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    if (UHorizontalBoxSlot* TitleTextSlot = TitleRow->AddChildToHorizontalBox(Title))
    {
        TitleTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TitleTextSlot->SetVerticalAlignment(VAlign_Center);
    }
    UButton* Close = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Close, FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.06f),
                      FLinearColor(0, 0, 0, 0), 0.f, FMargin(7.f, 1.f));
    UTextBlock* CloseX = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    CloseX->SetText(FText::FromString(TEXT("×")));
    CloseX->SetFont(BoldFont(18));
    CloseX->SetColorAndOpacity(FSlateColor(kMutedColor));
    Close->AddChild(CloseX);
    Close->OnClicked.AddDynamic(this, &UPlayloopFeedbackWidget::HandleCancelClicked);
    if (UHorizontalBoxSlot* CloseSlot = TitleRow->AddChildToHorizontalBox(Close))
    {
        CloseSlot->SetVerticalAlignment(VAlign_Center);
    }
    if (UVerticalBoxSlot* TitleSlot = Stack->AddChildToVerticalBox(TitleRow))
    {
        TitleSlot->SetPadding(FMargin(0, 0, 0, 16));
    }

    ShortTextInputs.Reset();
    LongTextInputs.Reset();
    OptionButtons.Reset();
    int32 ShortIdx = 0;
    int32 LongIdx = 0;

    for (int32 FieldIdx = 0; FieldIdx < Fields.Num(); ++FieldIdx)
    {
        const FPlayloopFeedbackField& Field = Fields[FieldIdx];

        UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Label->SetText(FText::FromString(Field.Label));
        Label->SetFont(BoldFont(14));
        Label->SetColorAndOpacity(FSlateColor(kLabelColor));
        if (UVerticalBoxSlot* LabelSlot = Stack->AddChildToVerticalBox(Label))
        {
            LabelSlot->SetPadding(FMargin(0, 16, 0, 7));
        }

        if (!Field.HelpText.IsEmpty())
        {
            UTextBlock* Help = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Help->SetText(FText::FromString(Field.HelpText));
            Help->SetFont(RegularFont(12));
            Help->SetColorAndOpacity(FSlateColor(kMutedColor));
            if (UVerticalBoxSlot* HelpSlot = Stack->AddChildToVerticalBox(Help))
            {
                HelpSlot->SetPadding(FMargin(0, 0, 0, 6));
            }
        }

        switch (Field.Kind)
        {
            case EPlayloopFeedbackFieldKind::ShortText:
            {
                UEditableTextBox* Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
                Input->WidgetStyle = InputStyle();
                Input->SetHintText(FText::FromString(Field.Placeholder.IsEmpty() ? TEXT("Tell us more...") : Field.Placeholder));
                Stack->AddChildToVerticalBox(Input);
                FieldKindIndex[FieldIdx] = ShortIdx++;
                ShortTextInputs.Add(Input);
                break;
            }
            case EPlayloopFeedbackFieldKind::LongText:
            {
                UMultiLineEditableTextBox* Input = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(UMultiLineEditableTextBox::StaticClass());
                Input->WidgetStyle = InputStyle();
                Input->SetHintText(FText::FromString(Field.Placeholder.IsEmpty() ? TEXT("Tell us more...") : Field.Placeholder));
                USizeBox* Tall = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                Tall->SetHeightOverride(108.f);
                Tall->SetContent(Input);
                Stack->AddChildToVerticalBox(Tall);
                FieldKindIndex[FieldIdx] = LongIdx++;
                LongTextInputs.Add(Input);
                break;
            }
            case EPlayloopFeedbackFieldKind::Rating1To5:
            {
                UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
                for (int32 V = 1; V <= 5; ++V)
                {
                    UPlayloopFeedbackOptionButton* Btn = WidgetTree->ConstructWidget<UPlayloopFeedbackOptionButton>(UPlayloopFeedbackOptionButton::StaticClass());
                    Btn->FieldIndex = FieldIdx;
                    Btn->IntValue = V;
                    Btn->Owner = this;
                    StyleFilledButton(Btn, kButtonSecondary, kButtonHover, kInputBorder, 1.f, FMargin(0.f, 11.f));
                    UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                    T->SetText(FText::FromString(FString::FromInt(V)));
                    T->SetFont(BoldFont(15));
                    T->SetColorAndOpacity(FSlateColor(kLabelColor));
                    Btn->AddChild(T);
                    Btn->OnClicked.AddDynamic(Btn, &UPlayloopFeedbackOptionButton::HandleClicked);
                    if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Btn))
                    {
                        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                        S->SetPadding(FMargin(V == 1 ? 0.f : 8.f, 0, 0, 0));
                    }
                    OptionButtons.Add(Btn);
                }
                Stack->AddChildToVerticalBox(Row);
                break;
            }
            case EPlayloopFeedbackFieldKind::YesNo:
            {
                UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
                const TCHAR* Labels[2] = { TEXT("Yes"), TEXT("No") };
                const TCHAR* Values[2] = { TEXT("yes"), TEXT("no") };
                for (int32 i = 0; i < 2; ++i)
                {
                    UPlayloopFeedbackOptionButton* Btn = WidgetTree->ConstructWidget<UPlayloopFeedbackOptionButton>(UPlayloopFeedbackOptionButton::StaticClass());
                    Btn->FieldIndex = FieldIdx;
                    Btn->StrValue = Values[i];
                    Btn->Owner = this;
                    StyleFilledButton(Btn, kButtonSecondary, kButtonHover, kInputBorder, 1.f, FMargin(0.f, 11.f));
                    UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                    T->SetText(FText::FromString(Labels[i]));
                    T->SetFont(BoldFont(15));
                    T->SetColorAndOpacity(FSlateColor(kLabelColor));
                    Btn->AddChild(T);
                    Btn->OnClicked.AddDynamic(Btn, &UPlayloopFeedbackOptionButton::HandleClicked);
                    if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Btn))
                    {
                        S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                        S->SetPadding(FMargin(i == 0 ? 0.f : 8.f, 0, 0, 0));
                    }
                    OptionButtons.Add(Btn);
                }
                Stack->AddChildToVerticalBox(Row);
                break;
            }
        }
    }

    // Cancel + Send row: both fill the width, Send in the accent colour.
    UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

    UButton* Cancel = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Cancel, kButtonSecondary, kButtonHover, kInputBorder, 1.f, FMargin(0.f, 11.f));
    UTextBlock* CancelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    CancelText->SetText(FText::FromString(TEXT("Cancel")));
    CancelText->SetFont(BoldFont(14));
    CancelText->SetColorAndOpacity(FSlateColor(kLabelColor));
    Cancel->AddChild(CancelText);
    Cancel->OnClicked.AddDynamic(this, &UPlayloopFeedbackWidget::HandleCancelClicked);
    if (UHorizontalBoxSlot* CancelSlot = ButtonRow->AddChildToHorizontalBox(Cancel))
    {
        CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        CancelSlot->SetPadding(FMargin(0, 0, 10, 0));
    }

    UButton* Send = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    StyleFilledButton(Send, kAccent, kAccentHover, FLinearColor(0, 0, 0, 0), 0.f, FMargin(0.f, 11.f));
    UTextBlock* SendText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    SendText->SetText(FText::FromString(TEXT("Send")));
    SendText->SetFont(BoldFont(14));
    SendText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    Send->AddChild(SendText);
    Send->OnClicked.AddDynamic(this, &UPlayloopFeedbackWidget::HandleSendClicked);
    if (UHorizontalBoxSlot* SendSlot = ButtonRow->AddChildToHorizontalBox(Send))
    {
        FSlateChildSize Wider(ESlateSizeRule::Fill);
        Wider.Value = 1.4f;
        SendSlot->SetSize(Wider);
    }

    // Required-field validation message, above the buttons. Hidden until Send
    // is blocked by an unanswered required field (see HandleSendClicked).
    UTextBlock* Err = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Err->SetText(FText::FromString(TEXT("Please answer the required fields before sending.")));
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

    // Footer: clickable "[mark] Powered by Playloop" badge, centered.
    // Gated on the server-resolved branding flag: paid studios can turn the
    // badge off; free studios always show it (default true before the
    // event-config fetch lands, matching Unity/Godot). Clicking opens
    // playloop.gg in the player's browser.
    const bool bShowBranding = Client ? Client->GetGameSettings().bShowBranding : true;
    if (bShowBranding)
    {
        // A borderless button so the row stays visually identical to the old
        // static footer but becomes clickable.
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
        FooterBtn->OnClicked.AddDynamic(this, &UPlayloopFeedbackWidget::HandleBrandingClicked);

        UHorizontalBox* FooterRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

        if (UTexture2D* LogoTex = GetBrandMarkIfReady())
        {
            UImage* Logo = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
            Logo->SetBrushFromTexture(LogoTex, /*bMatchSize=*/ false);
            Logo->SetDesiredSizeOverride(FVector2D(16.f, 16.f));
            if (UHorizontalBoxSlot* LogoSlot = FooterRow->AddChildToHorizontalBox(Logo))
            {
                LogoSlot->SetVerticalAlignment(VAlign_Center);
                LogoSlot->SetPadding(FMargin(0, 0, 6, 0));
            }
        }

        UTextBlock* Footer = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Footer->SetText(FText::FromString(TEXT("Powered by Playloop")));
        Footer->SetFont(RegularFont(12));
        Footer->SetColorAndOpacity(FSlateColor(kMutedColor));
        if (UHorizontalBoxSlot* FooterTextSlot = FooterRow->AddChildToHorizontalBox(Footer))
        {
            FooterTextSlot->SetVerticalAlignment(VAlign_Center);
        }

        FooterBtn->AddChild(FooterRow);

        if (UVerticalBoxSlot* FooterSlot = Stack->AddChildToVerticalBox(FooterBtn))
        {
            FooterSlot->SetPadding(FMargin(0, 14, 0, 0));
            FooterSlot->SetHorizontalAlignment(HAlign_Center);
        }
    }

    // Center the card on a dimmed full-screen backdrop so it reads as a
    // modal rather than stretching across the viewport.
    USizeBox* CardSizer = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSizer"));
    // Responsive width: cap at 560 on big screens, but shrink to fit narrow
    // viewports (leave a 24px gutter each side) down to a 280 floor, so the
    // card never overflows on mobile/handheld. Mirrors the Godot clamp.
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

void UPlayloopFeedbackWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    ShortTextInputs.Reset();
    LongTextInputs.Reset();
}

void UPlayloopFeedbackWidget::SetRatingValue(int32 FieldIndex, int32 Value)
{
    // Public hook for Blueprint subclasses. The BP rating widget
    // (5 buttons, each calling this with 1..5) wires straight in.
    if (RatingValues.IsValidIndex(FieldIndex))
    {
        RatingValues[FieldIndex] = Value;
    }
}

void UPlayloopFeedbackWidget::SetYesNoValue(int32 FieldIndex, const FString& Value)
{
    if (!YesNoValues.IsValidIndex(FieldIndex)) return;
    if (Value == TEXT("yes") || Value == TEXT("no"))
    {
        YesNoValues[FieldIndex] = Value;
    }
}

void UPlayloopFeedbackWidget::OnOptionSelected(UPlayloopFeedbackOptionButton* Btn)
{
    if (!Btn) return;

    if (Btn->StrValue.IsEmpty())
    {
        SetRatingValue(Btn->FieldIndex, Btn->IntValue);
    }
    else
    {
        SetYesNoValue(Btn->FieldIndex, Btn->StrValue);
    }

    // Re-highlight: the chosen button in this field gets the accent fill,
    // its siblings fall back to the secondary fill.
    for (UPlayloopFeedbackOptionButton* B : OptionButtons)
    {
        if (B && B->FieldIndex == Btn->FieldIndex)
        {
            if (B == Btn)
            {
                StyleFilledButton(B, kAccent, kAccentHover, FLinearColor(0, 0, 0, 0), 0.f, FMargin(0.f, 11.f));
            }
            else
            {
                StyleFilledButton(B, kButtonSecondary, kButtonHover, kInputBorder, 1.f, FMargin(0.f, 11.f));
            }
        }
    }
}

void UPlayloopFeedbackWidget::HandleCancelClicked()
{
    FPlayloopFeedbackResult Result;
    Result.bOk = false;
    Result.FormId = FormId;
    Result.ErrorCode = TEXT("dismissed");
    OnFeedbackResolved.Broadcast(Result);
    RemoveFromParent();
}

void UPlayloopFeedbackWidget::HandleBrandingClicked()
{
    FPlatformProcess::LaunchURL(TEXT("https://playloop.gg"), nullptr, nullptr);
}

void UPlayloopFeedbackOptionButton::HandleClicked()
{
    if (Owner)
    {
        Owner->OnOptionSelected(this);
    }
}

void UPlayloopFeedbackWidget::HandleSendClicked()
{
    if (Client == nullptr || FormId.IsEmpty()) return;

    // Clear any prior validation message; re-shown below if still blocked.
    if (ErrorText) ErrorText->SetVisibility(ESlateVisibility::Collapsed);

    TArray<FPlayloopFeedbackResponse> Responses;
    bool bHasMissingRequired = false;
    for (int32 Idx = 0; Idx < Fields.Num(); ++Idx)
    {
        const FPlayloopFeedbackField& Field = Fields[Idx];
        FString Value;
        switch (Field.Kind)
        {
            case EPlayloopFeedbackFieldKind::Rating1To5:
                if (RatingValues.IsValidIndex(Idx) && RatingValues[Idx] > 0)
                {
                    Value = FString::FromInt(RatingValues[Idx]);
                }
                break;
            case EPlayloopFeedbackFieldKind::YesNo:
                if (YesNoValues.IsValidIndex(Idx)) Value = YesNoValues[Idx];
                break;
            case EPlayloopFeedbackFieldKind::ShortText:
            {
                const int32 SIdx = FieldKindIndex.IsValidIndex(Idx) ? FieldKindIndex[Idx] : -1;
                if (SIdx >= 0 && ShortTextInputs.IsValidIndex(SIdx) && ShortTextInputs[SIdx])
                {
                    Value = ShortTextInputs[SIdx]->GetText().ToString();
                }
                break;
            }
            case EPlayloopFeedbackFieldKind::LongText:
            {
                const int32 LIdx = FieldKindIndex.IsValidIndex(Idx) ? FieldKindIndex[Idx] : -1;
                if (LIdx >= 0 && LongTextInputs.IsValidIndex(LIdx) && LongTextInputs[LIdx])
                {
                    Value = LongTextInputs[LIdx]->GetText().ToString();
                }
                break;
            }
        }

        if (Field.bRequired && Value.IsEmpty())
        {
            bHasMissingRequired = true;
            continue;
        }
        if (Value.IsEmpty()) continue;
        FPlayloopFeedbackResponse R;
        R.FieldId = Field.Id;
        R.Value = Value;
        Responses.Add(R);
    }

    if (bHasMissingRequired)
    {
        // Surface why Send did nothing so the player can fix it, and leave the
        // form open (parity with the Unity/Godot widgets, which flag the field).
        if (ErrorText) ErrorText->SetVisibility(ESlateVisibility::Visible);
        return;
    }
    if (Responses.Num() == 0)
    {
        HandleCancelClicked();
        return;
    }

    FOnPlayloopFeedbackSubmittedBP Forward;
    Forward.BindDynamic(this, &UPlayloopFeedbackWidget::HandleSubmitResult);
    Client->SubmitFeedback(FormId, Responses, Forward);
    RemoveFromParent();
}

void UPlayloopFeedbackWidget::HandleSubmitResult(const FPlayloopFeedbackResult& Result)
{
    OnFeedbackResolved.Broadcast(Result);
}

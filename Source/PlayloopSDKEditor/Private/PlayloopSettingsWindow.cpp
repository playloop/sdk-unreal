#include "PlayloopSettingsWindow.h"

#include "PlayloopSettings.h"
#include "PlayloopClient.h"
#include "PlayloopEditorPaths.h"
#include "PlayloopEditorResolve.h"
#include "PlayloopSDK.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Interfaces/IPluginManager.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "PlayloopSettingsWindow"

namespace
{
    // ── Brand palette (mirrors the Unity + Godot SDK settings windows) ──
    const FLinearColor CWindowBg(0.071f, 0.078f, 0.094f);
    const FLinearColor CCardBg(0.094f, 0.102f, 0.122f);
    const FLinearColor CInputBg(0.129f, 0.137f, 0.161f);
    const FLinearColor CAccent(0.40f, 0.62f, 1.0f);
    const FLinearColor CText(0.85f, 0.85f, 0.88f);
    const FLinearColor CMuted(0.55f, 0.57f, 0.62f);
    const FLinearColor CPillBg(0.25f, 0.25f, 0.27f);
    const FLinearColor CConnected(0.10f, 0.25f, 0.15f);
    const FLinearColor CConnectedText(0.40f, 1.0f, 0.50f);
    const FLinearColor CErrorBg(0.30f, 0.10f, 0.10f);
    const FLinearColor CErrorText(1.0f, 0.55f, 0.55f);

    TSharedPtr<FSlateStyleSet> GStyle;
    TWeakPtr<SWindow> GOpenWindow;

    void EnsureStyle()
    {
        if (GStyle.IsValid())
        {
            return;
        }
        GStyle = MakeShareable(new FSlateStyleSet("PlayloopEditorStyle"));
        GStyle->Set("Playloop.Solid", new FSlateColorBrush(FLinearColor::White));

        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PlayloopSDK"));
        if (Plugin.IsValid())
        {
            GStyle->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
            GStyle->Set(
                "Playloop.Wordmark",
                new FSlateImageBrush(
                    GStyle->RootToContentDir(TEXT("playloop_wordmark"), TEXT(".png")),
                    FVector2D(132.0f, 28.0f)));
        }
        FSlateStyleRegistry::RegisterSlateStyle(*GStyle);
    }

    const FSlateBrush* SolidBrush()
    {
        EnsureStyle();
        return GStyle->GetBrush("Playloop.Solid");
    }

    const FSlateBrush* WordmarkBrush()
    {
        EnsureStyle();
        return GStyle->GetBrush("Playloop.Wordmark");
    }

    UPlayloopSettings* Settings()
    {
        return GetMutableDefault<UPlayloopSettings>();
    }

    // A muted section heading ("CONNECTION", etc.).
    TSharedRef<SWidget> Heading(const FString& Text)
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Text.ToUpper()))
            .ColorAndOpacity(FSlateColor(CMuted))
            .Margin(FMargin(0.0f, 10.0f, 0.0f, 4.0f));
    }

    // A label + control row.
    TSharedRef<SWidget> Field(const FString& Label, TSharedRef<SWidget> Control)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(120.0f)
                [
                    SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FSlateColor(CText))
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                Control
            ];
    }

    // An editable text box wired to a settings string field.
    TSharedRef<SWidget> StringField(FString UPlayloopSettings::* Member, const FString& Hint, bool bPassword)
    {
        return SNew(SEditableTextBox)
            .Text(FText::FromString(Settings()->*Member))
            .HintText(FText::FromString(Hint))
            .IsPassword(bPassword)
            .OnTextCommitted_Lambda([Member](const FText& NewText, ETextCommit::Type)
            {
                Settings()->*Member = NewText.ToString().TrimStartAndEnd();
                Settings()->SaveConfig();
            });
    }

    // A checkbox wired to a settings bool field.
    TSharedRef<SWidget> BoolToggle(const FString& Label, bool UPlayloopSettings::* Member)
    {
        return SNew(SCheckBox)
            .IsChecked(Settings()->*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
            .OnCheckStateChanged_Lambda([Member](ECheckBoxState State)
            {
                Settings()->*Member = (State == ECheckBoxState::Checked);
                Settings()->SaveConfig();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString(Label))
                .ColorAndOpacity(FSlateColor(CText))
                .Margin(FMargin(4.0f, 0.0f, 16.0f, 0.0f))
            ];
    }

    // Fire GET /api/telemetry/resolve with the ingest key; the key identifies
    // the game (no slug needed), so the pill can report Connected + the game
    // name from the key alone.
    void VerifyConnection(TSharedPtr<STextBlock> PillText, TSharedPtr<SBorder> PillBox, TSharedPtr<STextBlock> Detail)
    {
        const UPlayloopSettings* S = Settings();
        if (!S || S->ApiKey.IsEmpty())
        {
            PillText->SetText(LOCTEXT("PillNotConfigured", "Not configured"));
            Detail->SetText(LOCTEXT("DetailNoKey", "Add an ingest key first."));
            return;
        }

        PillText->SetText(LOCTEXT("PillVerifying", "Verifying…"));
        Detail->SetText(FText::GetEmpty());

        PlayloopEditorResolve::Resolve(PlayloopEditorResolve::FOnResolved::CreateLambda(
            [PillText, PillBox, Detail](bool bOk, const FString& /*Slug*/, const FString& Name)
            {
                if (!PillText.IsValid() || !PillBox.IsValid() || !Detail.IsValid())
                {
                    return;
                }
                if (bOk)
                {
                    PillText->SetText(LOCTEXT("PillConnected", "Connected"));
                    PillText->SetColorAndOpacity(FSlateColor(CConnectedText));
                    PillBox->SetBorderBackgroundColor(CConnected);
                    Detail->SetText(Name.IsEmpty()
                        ? LOCTEXT("DetailConnected", "Key resolved.")
                        : FText::FromString(FString::Printf(TEXT("Connected to %s."), *Name)));
                }
                else
                {
                    PillText->SetText(LOCTEXT("PillInvalid", "Invalid key"));
                    PillText->SetColorAndOpacity(FSlateColor(CErrorText));
                    PillBox->SetBorderBackgroundColor(CErrorBg);
                    Detail->SetText(LOCTEXT("DetailInvalid", "Key is invalid or expired, or the server is unreachable. Grab the game's ingest key from its Connections, Unreal page on playloop.gg."));
                }
            }));
    }
}

namespace PlayloopSettingsWindow
{
    void Shutdown()
    {
        if (GStyle.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*GStyle);
            GStyle.Reset();
        }
    }

    void Open()
    {
        // Focus the existing window instead of stacking a second one.
        if (TSharedPtr<SWindow> Existing = GOpenWindow.Pin())
        {
            Existing->BringToFront();
            return;
        }

        EnsureStyle();

        TSharedPtr<SBorder> PillBox;
        TSharedPtr<STextBlock> PillText;
        TSharedPtr<STextBlock> Detail;

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("WindowTitle", "Playloop"))
            .ClientSize(FVector2D(560.0f, 640.0f))
            .SupportsMaximize(false);

        Window->SetContent(
            SNew(SBorder)
            .BorderImage(SolidBrush())
            .BorderBackgroundColor(CWindowBg)
            .Padding(18.0f)
            [
                SNew(SVerticalBox)

                // Header: wordmark + status pill.
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SImage).Image(WordmarkBrush())
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SAssignNew(PillBox, SBorder)
                        .BorderImage(SolidBrush())
                        .BorderBackgroundColor(CPillBg)
                        .Padding(FMargin(12.0f, 6.0f))
                        [
                            SAssignNew(PillText, STextBlock)
                            .Text(LOCTEXT("PillNotConfigured2", "Not configured"))
                            .ColorAndOpacity(FSlateColor(CText))
                        ]
                    ]
                ]

                // Detail line.
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SAssignNew(Detail, STextBlock)
                    .Text(FText::GetEmpty())
                    .ColorAndOpacity(FSlateColor(CMuted))
                    .AutoWrapText(true)
                ]

                // Action row.
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Verify", "Verify Connection"))
                        .OnClicked_Lambda([PillText, PillBox, Detail]()
                        {
                            VerifyConnection(PillText, PillBox, Detail);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("OpenDashboard", "Open Dashboard"))
                        .OnClicked_Lambda([]()
                        {
                            // Resolve the slug from the ingest key, then open the
                            // game's dashboard. Falls back to the games list if the
                            // key can't be resolved.
                            const UPlayloopSettings* S = Settings();
                            const FString Env = S ? S->ResolvedEnvironment() : FString();
                            PlayloopEditorResolve::Resolve(PlayloopEditorResolve::FOnResolved::CreateLambda(
                                [Env](bool /*bOk*/, const FString& Slug, const FString& /*Name*/)
                                {
                                    const FString DashUrl = PlayloopEditorPaths::DashboardForGame(Slug, Env);
                                    FPlatformProcess::LaunchURL(*DashUrl, nullptr, nullptr);
                                }));
                            return FReply::Handled();
                        })
                    ]
                ]

                // Connection.
                + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Connection"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                [Field(TEXT("Ingest key"), StringField(&UPlayloopSettings::ApiKey, TEXT("pl_ik_..."), true))]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                [Field(TEXT("Base URL"), StringField(&UPlayloopSettings::ApiBaseUrl, TEXT("https://playloop.gg"), false))]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                [Field(TEXT("Environment"), StringField(&UPlayloopSettings::Environment, TEXT("production / demo / dev / custom"), false))]

                // Auto-instrument.
                + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Auto-instrument"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()[BoolToggle(TEXT("Scene changes"), &UPlayloopSettings::bAutoInstrumentScenes)]
                    + SHorizontalBox::Slot().AutoWidth()[BoolToggle(TEXT("Focus"), &UPlayloopSettings::bAutoInstrumentFocus)]
                    + SHorizontalBox::Slot().AutoWidth()[BoolToggle(TEXT("Idle"), &UPlayloopSettings::bAutoInstrumentIdle)]
                    + SHorizontalBox::Slot().AutoWidth()[BoolToggle(TEXT("FPS"), &UPlayloopSettings::bAutoInstrumentFps)]
                    + SHorizontalBox::Slot().AutoWidth()[BoolToggle(TEXT("Memory"), &UPlayloopSettings::bAutoInstrumentMemory)]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AutoNote", "Each event has an independent kill-switch. Memory pressure is off by default. It's noisy on low-memory devices."))
                    .ColorAndOpacity(FSlateColor(CMuted))
                    .AutoWrapText(true)
                ]
            ]);

        // Reflect the configured/unconfigured state on open.
        if (Settings() && !Settings()->ApiKey.IsEmpty())
        {
            Detail->SetText(LOCTEXT("DetailReady", "Press Verify Connection to confirm the key."));
        }
        else
        {
            Detail->SetText(LOCTEXT("DetailConfigure", "Paste this game's ingest key to begin."));
        }

        GOpenWindow = Window;
        FSlateApplication::Get().AddWindow(Window);
    }
}

#undef LOCTEXT_NAMESPACE

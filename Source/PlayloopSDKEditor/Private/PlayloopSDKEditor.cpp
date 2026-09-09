#include "PlayloopSDKEditor.h"
#include "PlayloopEditorPaths.h"
#include "PlayloopEditorResolve.h"
#include "PlayloopClient.h"
#include "PlayloopSettings.h"
#include "PlayloopSDK.h"
#include "PlayloopSettingsWindow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Misc/MessageDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "PlayloopSDKEditor"

namespace
{
    void OpenUrl(const TCHAR* Url)
    {
        FString Out(Url);
        FPlatformProcess::LaunchURL(*Out, nullptr, nullptr);
    }

    void OpenDashboardForGame()
    {
        // The SDK no longer stores a slug. Resolve it from the ingest key, then
        // open the game's dashboard (falls back to the games list on failure).
        const UPlayloopSettings* Settings = GetDefault<UPlayloopSettings>();
        const FString Env = Settings ? Settings->ResolvedEnvironment() : FString();
        PlayloopEditorResolve::Resolve(PlayloopEditorResolve::FOnResolved::CreateLambda(
            [Env](bool /*bOk*/, const FString& Slug, const FString& /*Name*/)
            {
                const FString Url = PlayloopEditorPaths::DashboardForGame(Slug, Env);
                FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
            }));
    }

    void SendTestEvent()
    {
        UPlayloopClient* Client = UPlayloopClient::GetCurrent();
        if (!Client)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
                "PlayloopNoClient",
                "No active Playloop client. Start Play in Editor first, or configure auto-start in Project Settings -> Plugins -> Playloop."));
            return;
        }
        Client->Track(TEXT("test_event"), TEXT("{\"source\":\"editor-menu\"}"));
        Client->Flush();
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT(
            "PlayloopSent",
            "Test event sent. Look for a 'test_event' row at playloop.gg in the next minute."));
    }

    void SetEnvironment(FString Env)
    {
        UPlayloopSettings* Settings = GetMutableDefault<UPlayloopSettings>();
        if (!Settings) return;
        Settings->Environment = Env;
        Settings->SaveConfig();
        UE_LOG(LogPlayloop, Log, TEXT("Playloop environment set to '%s'."), *Env);
    }

    // Modal text-entry prompt for a custom environment string. Unreal
    // ships no built-in input dialog, so we build a small modal Slate
    // window. Mirrors the Unity / Godot "Custom…" flow: pre-fill with the
    // current environment, apply the trimmed result on accept.
    void PromptCustomEnvironment()
    {
        const UPlayloopSettings* Settings = GetDefault<UPlayloopSettings>();
        const FString Current = Settings ? Settings->Environment : FString();

        TSharedRef<SEditableTextBox> InputBox = SNew(SEditableTextBox)
            .Text(FText::FromString(Current))
            .MinDesiredWidth(260.0f);

        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("PlayloopCustomEnvTitle", "Set Custom Environment"))
            .SizingRule(ESizingRule::Autosized)
            .SupportsMaximize(false)
            .SupportsMinimize(false);

        bool bAccepted = false;

        Window->SetContent(
            SNew(SBox)
            .Padding(12.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PlayloopCustomEnvPrompt", "Environment name stamped on session_start:"))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [
                    InputBox
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PlayloopCustomEnvCancel", "Cancel"))
                        .OnClicked_Lambda([&Window]()
                        {
                            Window->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PlayloopCustomEnvOk", "Set"))
                        .OnClicked_Lambda([&Window, &bAccepted]()
                        {
                            bAccepted = true;
                            Window->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                    ]
                ]
            ]);

        // Blocks until the window is destroyed, so the captured locals
        // stay valid for the read below.
        FSlateApplication::Get().AddModalWindow(Window, nullptr);

        if (bAccepted)
        {
            const FString NewEnv = InputBox->GetText().ToString().TrimStartAndEnd();
            if (!NewEnv.IsEmpty())
            {
                SetEnvironment(NewEnv);
            }
        }
    }
}

void FPlayloopSDKEditorModule::StartupModule()
{
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPlayloopSDKEditorModule::RegisterMenus));
}

void FPlayloopSDKEditorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UnregisterMenus();
    PlayloopSettingsWindow::Shutdown();
}

void FPlayloopSDKEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    // Inject a top-level "Playloop" menu next to Window / Help in the
    // main editor menu bar. ToolMenus is the stable UE5 path.
    UToolMenu* MenuBar = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
    if (!MenuBar)
    {
        return;
    }

    FToolMenuSection& BarSection = MenuBar->FindOrAddSection("PlayloopMenuBar");
    BarSection.AddSubMenu(
        "Playloop",
        LOCTEXT("PlayloopMenuLabel", "Playloop"),
        LOCTEXT("PlayloopMenuTooltip", "Playloop SDK tools and links."),
        FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
        {
            FToolMenuSection& Actions = SubMenu->FindOrAddSection("Actions");
            Actions.AddMenuEntry(
                "PlayloopSettings",
                LOCTEXT("PlayloopSettingsLabel", "Settings…"),
                LOCTEXT("PlayloopSettingsTip", "Open the Playloop settings window."),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateStatic(&PlayloopSettingsWindow::Open)));
            Actions.AddMenuEntry(
                "PlayloopSendTest",
                LOCTEXT("PlayloopSendTestLabel", "Send Test Event"),
                LOCTEXT("PlayloopSendTestTip", "Fire a single test_event from the editor (requires Play in Editor)."),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateStatic(&SendTestEvent)));
            Actions.AddSubMenu(
                "PlayloopEnv",
                LOCTEXT("PlayloopEnvLabel", "Set Environment"),
                LOCTEXT("PlayloopEnvTip", "Pick the environment stamped on session_start."),
                FNewToolMenuDelegate::CreateLambda([](UToolMenu* EnvMenu)
                {
                    FToolMenuSection& EnvSection = EnvMenu->FindOrAddSection("Envs");
                    auto AddEnv = [&EnvSection](const TCHAR* Id, const TCHAR* Label, const TCHAR* Value)
                    {
                        EnvSection.AddMenuEntry(
                            FName(Id),
                            FText::FromString(Label),
                            FText::FromString(FString::Printf(TEXT("Sets the environment to '%s'."), Value)),
                            FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([Value]() { SetEnvironment(FString(Value)); })));
                    };
                    AddEnv(TEXT("EnvProd"), TEXT("Production"), TEXT("production"));
                    AddEnv(TEXT("EnvDemo"), TEXT("Demo"), TEXT("demo"));
                    AddEnv(TEXT("EnvDev"), TEXT("Dev"), TEXT("dev"));
                    EnvSection.AddMenuEntry(
                        "EnvCustom",
                        LOCTEXT("PlayloopEnvCustomLabel", "Custom…"),
                        LOCTEXT("PlayloopEnvCustomTip", "Enter a custom environment string."),
                        FSlateIcon(),
                        FUIAction(FExecuteAction::CreateStatic(&PromptCustomEnvironment)));
                }));

            FToolMenuSection& Links = SubMenu->FindOrAddSection("Links");
            Links.AddSubMenu(
                "PlayloopDocsSubmenu",
                LOCTEXT("PlayloopDocsLabel", "Docs"),
                LOCTEXT("PlayloopDocsTip", "Playloop documentation and public roadmap."),
                FNewToolMenuDelegate::CreateLambda([](UToolMenu* DocsMenu)
                {
                    FToolMenuSection& DocsSection = DocsMenu->FindOrAddSection("DocsLinks");
                    DocsSection.AddMenuEntry(
                        "PlayloopOpenDocs",
                        LOCTEXT("PlayloopOpenDocsLabel", "Open Docs"),
                        LOCTEXT("PlayloopOpenDocsTip", "Open playloop.gg/docs."),
                        FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([]() { OpenUrl(PlayloopEditorPaths::DocsUrl); })));
                    DocsSection.AddMenuEntry(
                        "PlayloopOpenRoadmap",
                        LOCTEXT("PlayloopOpenRoadmapLabel", "Open Roadmap"),
                        LOCTEXT("PlayloopOpenRoadmapTip", "Open the public Playloop roadmap."),
                        FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([]() { OpenUrl(PlayloopEditorPaths::RoadmapUrl); })));
                }));
            Links.AddMenuEntry(
                "PlayloopOpenDashboard",
                LOCTEXT("PlayloopOpenDashboardLabel", "Open Dashboard for This Game"),
                LOCTEXT("PlayloopOpenDashboardTip", "Open playloop.gg/games/<slug> in your browser."),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateStatic(&OpenDashboardForGame)));
            Links.AddMenuEntry(
                "PlayloopOpenRepo",
                LOCTEXT("PlayloopOpenRepoLabel", "Open GitHub Repo"),
                LOCTEXT("PlayloopOpenRepoTip", "Open the playloop-sdk-unreal repo."),
                FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([]() { OpenUrl(PlayloopEditorPaths::RepoUrl); })));
        }),
        false,
        FSlateIcon());
}

void FPlayloopSDKEditorModule::UnregisterMenus()
{
    UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPlayloopSDKEditorModule, PlayloopSDKEditor)

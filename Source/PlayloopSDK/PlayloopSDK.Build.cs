using UnrealBuildTool;

public class PlayloopSDK : ModuleRules
{
    public PlayloopSDK(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings",
            // UMG / Slate are the Unreal UI modules; the default
            // UPlayloopFeedbackWidget builds against them programmatically.
            // Studios who only use SubmitFeedback() and skip the widget pay no
            // runtime cost: UMG ships with the engine and almost every game
            // already links it.
            "UMG",
            "Slate",
            "SlateCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "HTTP",
            "Json",
            "JsonUtilities",
            "Projects",
            // ApplicationCore backs FSlateApplication's activation + last-user-
            // interaction signals the auto-instrument driver reads for the
            // focus + idle events.
            "ApplicationCore",
            // InputCore provides EKeys, used by the feedback widget's
            // NativeOnKeyDown (Escape dismiss).
            "InputCore",
        });
    }
}

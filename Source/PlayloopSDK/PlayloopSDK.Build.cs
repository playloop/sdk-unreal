using UnrealBuildTool;

public class PlayloopSDK : ModuleRules
{
    public PlayloopSDK(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        // each file keeps its own helpers in an anonymous namespace (NowMs, the widget's colours); a unity blob that puts
        // two of them together fails to compile (UE 5.8 on Linux groups them that way)
        bUseUnity = false;

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

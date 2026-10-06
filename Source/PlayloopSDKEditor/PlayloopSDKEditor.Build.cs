using UnrealBuildTool;

public class PlayloopSDKEditor : ModuleRules
{
    public PlayloopSDKEditor(ReadOnlyTargetRules Target) : base(Target)
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
            "PlayloopSDK",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UnrealEd",
            "ToolMenus",
            "EditorStyle",
            "EditorSubsystem",
            "Projects",
            "Settings",
            "InputCore",
            "LevelEditor",
            "DeveloperSettings",
            "HTTP",
            "Json",
        });
    }
}

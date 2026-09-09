using UnrealBuildTool;

public class PlayloopSDKEditor : ModuleRules
{
    public PlayloopSDKEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

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

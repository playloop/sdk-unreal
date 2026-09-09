using UnrealBuildTool;

public class PlayloopSDKTests : ModuleRules
{
    public PlayloopSDKTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        // The specs exercise PlayloopSDK internals (crash handler, event queue,
        // experiments), so they include the runtime module's *private* headers.
        PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "..", "PlayloopSDK", "Private"));

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "PlayloopSDK",
            "Json",
            "JsonUtilities",
            "HTTP",
        });
    }
}

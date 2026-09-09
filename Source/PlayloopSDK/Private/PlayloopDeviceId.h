#pragma once

#include "CoreMinimal.h"

/**
 * Resolves a stable device id for the player, persisted to
 * `<ProjectSavedDir>/Playloop/device.id`. First-run generates a v4 GUID.
 *
 * Stable across runs and sessions; rotates only when the user wipes
 * their save dir. Matches the lifetime semantics of the Unity / Godot
 * SDKs.
 */
class PLAYLOOPSDK_API FPlayloopDeviceId
{
public:
    /** Get-or-generate. Thread-safe under the internal mutex. */
    static FString Resolve();

    /** Override the persisted id (used by tests). */
    static void OverrideForTesting(const FString& Id);

    /** Path to the device.id file (uses ProjectSavedDir; safe at runtime). */
    static FString GetPersistPath();
};

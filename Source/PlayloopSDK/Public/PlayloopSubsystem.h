#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayloopSubsystem.generated.h"

class UPlayloopClient;

/**
 * Auto-constructs UPlayloopClient on GameInstance init when
 * UPlayloopSettings.bAutoStart is true. Reads settings from the
 * project's Plugins -> Playloop config and calls Start().
 *
 * Disable bAutoStart and instantiate UPlayloopClient manually if you
 * need to defer Start() until after the player logs in or chooses a
 * tester handle.
 */
UCLASS()
class PLAYLOOPSDK_API UPlayloopSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category = "Playloop")
    UPlayloopClient* GetClient() const { return Client; }

    /**
     * Cross-game player identity.
     * Set the player's caller-supplied linked id (typically an email
     * or account id). Persists across sessions in
     * `<ProjectSavedDir>/Playloop/identity.json`. Pass empty string to
     * clear. The next session-create telemetry POST includes the
     * value so the server-side resolver can stitch cross-game identity.
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Identity")
    void SetLinkedId(const FString& LinkedId);

    /**
     * Set the player's tracking-consent preference. Allowed values:
     * "anonymous", "studio-wide", "cross-platform", "opt-out". Other
     * values are persisted as-is and the server interprets unknown
     * values as "anonymous".
     */
    UFUNCTION(BlueprintCallable, Category = "Playloop|Identity")
    void SetConsent(const FString& Level);

    /** Read the persisted consent level (defaults to "anonymous"). */
    UFUNCTION(BlueprintPure, Category = "Playloop|Identity")
    FString GetConsent() const;

private:
    UPROPERTY()
    TObjectPtr<UPlayloopClient> Client;
};

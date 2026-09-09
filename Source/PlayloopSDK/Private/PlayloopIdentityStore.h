#pragma once

#include "CoreMinimal.h"

/**
 * Cross-game player identity storage for the Unreal SDK.
 *
 * Two pieces of opt-in identity state persist across sessions:
 *
 *   * LinkedId: caller-supplied identity (email, account id, etc.).
 *   * ConsentLevel: player's tracking preference. One of
 *                    "anonymous" | "studio-wide" | "cross-platform" | "opt-out".
 *                    Defaults to "anonymous".
 *
 * Both back to `<ProjectSavedDir>/Playloop/identity.json` and mirror to
 * a per-process static cache so reads stay cheap. A failed write
 * (read-only filesystem) falls through to the cache only.
 *
 * Identity rides on the first session-create telemetry POST. Subsequent
 * flushes during the same session reuse the cached server-issued
 * `sessionId` and don't re-send identity.
 */
class FPlayloopIdentityStore
{
public:
    /** Get the persisted linked id, or empty string if nothing's set. */
    static FString GetLinkedId();

    /** Persist a linked id, or clear via an empty / whitespace-only value. */
    static void SetLinkedId(const FString& LinkedId);

    /** Get the persisted consent level. Always returns one of the four
     *  canonical values; defaults to "anonymous" when nothing's set. */
    static FString GetConsent();

    /** Persist a consent level. The server interprets unknown values
     *  as "anonymous". */
    static void SetConsent(const FString& Level);

    /** Test-only: clear both the on-disk file + the cache. */
    static void ResetForTesting();

    /** Absolute path of the on-disk identity file. Public so the file's
     *  read-through / write-through helpers (free functions in the .cpp's
     *  anonymous namespace) can resolve it. */
    static FString GetPersistPath();
};

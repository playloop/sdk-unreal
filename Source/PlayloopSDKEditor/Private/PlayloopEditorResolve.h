#pragma once

#include "CoreMinimal.h"

/**
 * Editor-side one-shot resolve of the game identity from the ingest key
 * (GET /api/telemetry/resolve). Used by the settings window's Verify
 * Connection + the "Open Dashboard for This Game" links. The SDK no longer
 * stores a slug/id, so the editor resolves it from the key on demand.
 */
namespace PlayloopEditorResolve
{
    /** Fires on the game thread. bOk is true on a 200; Slug/Name are the
     *  resolved values (empty on failure or an empty 200 body). */
    DECLARE_DELEGATE_ThreeParams(FOnResolved, bool /*bOk*/, const FString& /*Slug*/, const FString& /*Name*/);

    /** Fire the resolve with the configured ingest key + base URL. No-op
     *  callback (bOk=false) when no key is set. */
    void Resolve(FOnResolved OnDone);
}

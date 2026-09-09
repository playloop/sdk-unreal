#pragma once

#include "CoreMinimal.h"

/**
 * Branded Playloop settings window (Slate). The themed standalone window that
 * matches the Unity + Godot SDK settings panels: dark palette, wordmark
 * lockup, status pill, the connection fields + auto-instrument toggles, and a
 * Verify Connection button that resolves the game from the ingest key.
 *
 * Opened from the editor "Playloop > Settings…" menu entry instead of the
 * raw Project Settings details panel.
 */
namespace PlayloopSettingsWindow
{
    /** Open the window (or focus it if already open). */
    void Open();

    /** Unregister the Slate style set. Call from the editor module's shutdown. */
    void Shutdown();
}

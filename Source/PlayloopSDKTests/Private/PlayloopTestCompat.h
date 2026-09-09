#pragma once

#include "Misc/AutomationTest.h"
#include "Runtime/Launch/Resources/Version.h"

// UE 5.5 replaced the enum member EAutomationTestFlags::ApplicationContextMask
// with the namespace-level constexpr EAutomationTestFlags_ApplicationContextMask,
// and UE 5.8 removed the old member outright. This alias keeps the spec files
// building across UE 5.3 – 5.8.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
    #define PLAYLOOP_APP_CONTEXT_MASK EAutomationTestFlags_ApplicationContextMask
#else
    #define PLAYLOOP_APP_CONTEXT_MASK EAutomationTestFlags::ApplicationContextMask
#endif

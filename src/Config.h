#pragma once

// Central compile-time configuration for IRene.
//
// The plugin's name, manufacturer and codes are defined once, at the top of
// CMakeLists.txt, and arrive here as preprocessor definitions.

namespace irene
{
    /** Number of IR slots. All slot code (DSP, parameters, UI, state, presets)
        is generated in loops over this constant. */
    inline constexpr int kNumSlots = 3;

    inline constexpr const char* kPluginName = IRENE_PLUGIN_NAME;
    inline constexpr const char* kCompanyName = IRENE_COMPANY_NAME;
    inline constexpr const char* kVersionString = IRENE_VERSION_STRING;

    /** Version of the plugin-state format written by getStateInformation. */
    inline constexpr int kStateVersion = 1;
}

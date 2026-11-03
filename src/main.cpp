#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

// This macro creates the JUCE application wrapper that the OS expects.
START_JUCE_APPLICATION (NAMBerry)

// The JUCE Application framework requires an App class, but for a plugin
// we typically just want the plugin framework. The juce_add_plugin CMake
// macro handles the actual plugin entry points (VST3 entry, AU bundle, etc.)
// This main.cpp is a minimal placeholder to satisfy the build.

namespace
{
    class NAMBerryApp : public juce::Application
    {
    public:
        NAMBerryApp() = default;
        const char* getApplicationName() override { return "NAMBerry"; }
        const char* getManufacturerName() override { return "namberry"; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise(const juce::String& commandLine) override
        {
            // We don't need a UI for the plugin base; the plugin formats
            // (VST3, AU, AAX, Standalone) provide their own entry points.
            // This is kept minimal so the CMake build linking JUCE plugins works.
        }

    void shutdown() override {}
    void systemRequestedUpdate(bool requestedByOS) override {}
    };// NAMBerryApp
}
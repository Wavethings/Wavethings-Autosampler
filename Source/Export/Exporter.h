#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Core/Instrument.h"

namespace wt
{
    struct ExportOptions
    {
        juce::File convertWithMossExecutable;
        std::function<void (const juce::String&)> log;
    };

    struct ExportResult
    {
        bool ok = false;
        juce::String message;
        juce::File mainFile;

        static ExportResult success (const juce::File& f, const juce::String& extra = {})
        {
            return { true, f.getFullPathName() + (extra.isNotEmpty() ? "  (" + extra + ")" : juce::String()), f };
        }
        static ExportResult failure (const juce::String& m) { return { false, m, {} }; }
    };

    /** One sampler format. Every exporter writes into its own sub folder (or file) of the instrument folder.
        The master samples (instrument.sampleFolder) are never modified. */
    class Exporter
    {
    public:
        virtual ~Exporter() = default;
        virtual juce::String getId() const = 0;
        virtual juce::String getDisplayName() const = 0;
        virtual juce::String getDescription() const = 0;
        virtual bool requiresConvertWithMoss() const { return false; }
        virtual ExportResult exportInstrument (const Instrument& instrument, const ExportOptions& options) = 0;
    };

    struct ExporterInfo
    {
        juce::String id, name, description;
        bool viaConvertWithMoss = false;
    };

    std::vector<ExporterInfo> getAvailableExporters();
    std::unique_ptr<Exporter> createExporter (const juce::String& id);

    // Helpers shared by the exporters ------------------------------------------------
    namespace ExportHelpers
    {
        juce::String safeName (const juce::String& name);
        juce::String formatDouble (double value, int decimals = 6);
        juce::String escapeXml (const juce::String& text);

        /** Copies the master samples into 'folder' (optionally with another extension or name prefix). */
        bool copySamples (const Instrument& instrument, const juce::File& folder, juce::String& error,
                          const juce::String& extension = ".wav");

        juce::File uniqueFile (const juce::File& folder, const juce::String& baseName, const juce::String& extension);

        /** Writes text as UTF-8 without BOM. */
        bool writeText (const juce::File& file, const juce::String& text);
    }

    /** Embedded template files (Ableton, Kontakt). */
    namespace Templates
    {
        juce::String get (const juce::String& originalFileName);
    }
}

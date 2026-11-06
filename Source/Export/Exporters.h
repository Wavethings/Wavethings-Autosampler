#pragma once

#include "Exporter.h"
#include "../Core/Text.h"

namespace wt
{
#define WT_DECLARE_EXPORTER(ClassName, id, name, description)                                   \
    class ClassName : public Exporter                                                           \
    {                                                                                           \
    public:                                                                                     \
        juce::String getId() const override { return id; }                                      \
        juce::String getDisplayName() const override { return juce::String (name); }  \
        juce::String getDescription() const override { return juce::String (description); } \
        ExportResult exportInstrument (const Instrument&, const ExportOptions&) override;       \
    };

    WT_DECLARE_EXPORTER (SfzExporter, "sfz", "SFZ (.sfz)",
        "Formato abierto. Lo cargan Reason NN-XT, Bitwig, Cubase/HALion, FL Studio DirectWave, sforzando, TAL, etc.")

    WT_DECLARE_EXPORTER (DecentSamplerExporter, "decentsampler", "Decent Sampler (.dspreset)",
        "Preset con interfaz (Attack, Release, Volumen) para el plugin gratuito Decent Sampler.")

    WT_DECLARE_EXPORTER (BitwigExporter, "bitwig", "Bitwig / Studio One",
        "Archivo .multisample: Sampler de Bitwig Studio y Sample One XT / Presence XT de Studio One.")

    WT_DECLARE_EXPORTER (AbletonExporter, "ableton", "Ableton Sampler (.adv)",
        "Preset de Sampler. Copia la carpeta 'Ableton' a tu User Library.")

    WT_DECLARE_EXPORTER (MpcExporter, "mpc", "Akai MPC (.xpm)",
        "Programa Keygroup para MPC One/Live/X/Key y MPC Software (hasta 8 capas de velocidad).")

    WT_DECLARE_EXPORTER (ExsExporter, "exs", "Logic Sampler (.exs)",
        "Instrumento para el Sampler de Logic Pro y MainStage.")

    WT_DECLARE_EXPORTER (KontaktExporter, "kontakt", "Kontakt (.nki)",
        "Instrumento .nki en el formato de Kontakt 1 (el único documentado), que las versiones modernas de Kontakt abren."_u8)

    WT_DECLARE_EXPORTER (Sf2Exporter, "sf2", "SoundFont 2 (.sf2)",
        "Archivo único con todas las muestras: FL Studio, Reason, Cubase, Kontakt (importar), Logic, MuseScore..."_u8)

#undef WT_DECLARE_EXPORTER

    /** Formats that are written by Jürgen Moßgraber's ConvertWithMoss (if it is installed).
        We hand it a Bitwig .multisample and let its command line interface do the rest. */
    class MossBridgeExporter : public Exporter
    {
    public:
        struct Target
        {
            juce::String prefix;        // destination name on the ConvertWithMoss command line
            juce::String displayName;
            juce::String description;
        };

        static const std::vector<Target>& getTargets();
        static juce::File findConvertWithMoss (const juce::File& configured);

        explicit MossBridgeExporter (Target t) : target (std::move (t)) {}

        juce::String getId() const override { return "moss:" + target.prefix; }
        juce::String getDisplayName() const override { return target.displayName; }
        juce::String getDescription() const override { return target.description; }
        bool requiresConvertWithMoss() const override { return true; }
        ExportResult exportInstrument (const Instrument&, const ExportOptions&) override;

        /** Writes the Bitwig file used as the hand-over format. */
        static ExportResult writeBitwigFile (const Instrument& instrument, const juce::File& file);

    private:
        Target target;
    };
}

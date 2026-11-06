#include "Exporters.h"

namespace wt
{
    static juce::XmlElement* addKnob (juce::XmlElement& tab, int x, const juce::String& label, double minV, double maxV,
                                      double value, const juce::String& parameter)
    {
        auto* knob = tab.createNewChildElement ("labeled-knob");
        knob->setAttribute ("x", x);
        knob->setAttribute ("y", 60);
        knob->setAttribute ("width", 90);
        knob->setAttribute ("height", 100);
        knob->setAttribute ("label", label);
        knob->setAttribute ("type", "float");
        knob->setAttribute ("minValue", ExportHelpers::formatDouble (minV, 3));
        knob->setAttribute ("maxValue", ExportHelpers::formatDouble (maxV, 3));
        knob->setAttribute ("value", ExportHelpers::formatDouble (value, 3));
        knob->setAttribute ("textColor", "FFEDEDED");
        knob->setAttribute ("trackForegroundColor", "FFFF7A1A");
        knob->setAttribute ("trackBackgroundColor", "FF3A3A3A");

        auto* binding = knob->createNewChildElement ("binding");
        binding->setAttribute ("type", "amp");
        binding->setAttribute ("level", "instrument");
        binding->setAttribute ("position", 0);
        binding->setAttribute ("parameter", parameter);
        return knob;
    }

    ExportResult DecentSamplerExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        using namespace ExportHelpers;

        juce::XmlElement root ("DecentSampler");
        root.setAttribute ("minVersion", "1.0.0");

        const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;

        // UI ------------------------------------------------------------------------
        auto* ui = root.createNewChildElement ("ui");
        ui->setAttribute ("width", 812);
        ui->setAttribute ("height", 375);
        ui->setAttribute ("bgColor", "FF161616");
        auto* tab = ui->createNewChildElement ("tab");
        tab->setAttribute ("name", "main");

        auto* title = tab->createNewChildElement ("label");
        title->setAttribute ("x", 20);
        title->setAttribute ("y", 12);
        title->setAttribute ("width", 772);
        title->setAttribute ("height", 30);
        title->setAttribute ("text", inst.name);
        title->setAttribute ("textSize", 22);
        title->setAttribute ("textColor", "FFFF7A1A");
        title->setAttribute ("hAlign", "left");

        addKnob (*tab, 20, "Attack", 0.0, 5.0, 0.001, "ENV_ATTACK");
        addKnob (*tab, 120, "Decay", 0.0, 10.0, 0.0, "ENV_DECAY");
        addKnob (*tab, 220, "Sustain", 0.0, 1.0, 1.0, "ENV_SUSTAIN");
        addKnob (*tab, 320, "Release", 0.0, 10.0, release, "ENV_RELEASE");

        auto* volume = addKnob (*tab, 450, "Volumen", 0.0, 1.0, 0.8, "AMP_VOLUME");
        if (auto* b = volume->getChildByName ("binding"))
        {
            b->setAttribute ("translation", "linear");
            b->setAttribute ("translationOutputMin", 0);
            b->setAttribute ("translationOutputMax", 1);
        }

        // Groups --------------------------------------------------------------------
        auto* groups = root.createNewChildElement ("groups");
        groups->setAttribute ("attack", "0.001");
        groups->setAttribute ("decay", "0");
        groups->setAttribute ("sustain", "1");
        groups->setAttribute ("release", formatDouble (release, 3));
        groups->setAttribute ("volume", "0.8");

        const auto layers = inst.getZonesByLayer();
        for (size_t l = 0; l < layers.size(); ++l)
        {
            if (layers[l].empty())
                continue;

            auto* group = groups->createNewChildElement ("group");
            group->setAttribute ("name", "Velocidad " + juce::String (layers[l].front()->lowVelocity) + "-" + juce::String (layers[l].front()->highVelocity));

            for (auto* z : layers[l])
            {
                auto* sample = group->createNewChildElement ("sample");
                sample->setAttribute ("path", "Samples/" + z->fileName);
                sample->setAttribute ("rootNote", z->rootNote);
                sample->setAttribute ("loNote", z->lowKey);
                sample->setAttribute ("hiNote", z->highKey);
                sample->setAttribute ("loVel", z->lowVelocity);
                sample->setAttribute ("hiVel", z->highVelocity);
                sample->setAttribute ("start", 0);
                sample->setAttribute ("end", (int) (z->numFrames - 1));

                if (z->oneShot)
                {
                    sample->setAttribute ("ampEnvEnabled", "false");
                    sample->setAttribute ("loopEnabled", "false");
                }
                else if (z->loop.enabled)
                {
                    sample->setAttribute ("loopEnabled", "true");
                    sample->setAttribute ("loopStart", (int) z->loop.start);
                    sample->setAttribute ("loopEnd", (int) z->loop.end);
                    sample->setAttribute ("loopCrossfade", 0);
                }
                else
                {
                    sample->setAttribute ("loopEnabled", "false");
                }
            }
        }

        const auto file = inst.folder.getChildFile (safeName (inst.name) + ".dspreset");
        juce::XmlElement::TextFormat format;
        format.customEncoding = "UTF-8";
        format.lineWrapLength = 100000;
        if (! root.writeTo (file, format))
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        return ExportResult::success (file, "usa la carpeta Samples/");
    }
}

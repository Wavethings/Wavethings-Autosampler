#include "Exporters.h"
#include <map>

namespace wt
{
    static juce::String createBitwigXml (const Instrument& inst)
    {
        using namespace ExportHelpers;

        juce::XmlElement root ("multisample");
        root.setAttribute ("name", inst.name);
        root.createNewChildElement ("generator")->addTextElement ("Wavethings Autosampler");
        root.createNewChildElement ("category")->addTextElement (inst.category);
        root.createNewChildElement ("creator")->addTextElement (inst.creator);
        root.createNewChildElement ("description")->addTextElement (inst.description);
        auto* keywords = root.createNewChildElement ("keywords");
        keywords->createNewChildElement ("keyword")->addTextElement ("Wavethings");

        const auto layers = inst.getZonesByLayer();
        int groupIndex = 0;
        std::map<int, int> layerToGroup;
        for (size_t l = 0; l < layers.size(); ++l)
        {
            if (layers[l].empty())
                continue;
            auto* group = root.createNewChildElement ("group");
            group->setAttribute ("name", "Velocidad " + juce::String (layers[l].front()->lowVelocity) + "-" + juce::String (layers[l].front()->highVelocity));
            group->setAttribute ("color", "ff7a1a");
            layerToGroup[(int) l] = groupIndex++;
        }

        for (auto& z : inst.zones)
        {
            auto* sample = root.createNewChildElement ("sample");
            sample->setAttribute ("file", z.fileName);
            sample->setAttribute ("group", layerToGroup[z.layerIndex]);
            sample->setAttribute ("sample-start", formatDouble (0.0, 3));
            sample->setAttribute ("sample-stop", formatDouble ((double) z.numFrames, 3));
            sample->setAttribute ("reverse", "false");

            auto* key = sample->createNewChildElement ("key");
            key->setAttribute ("low", z.lowKey);
            key->setAttribute ("low-fade", 0);
            key->setAttribute ("root", z.rootNote);
            key->setAttribute ("high", z.highKey);
            key->setAttribute ("high-fade", 0);
            key->setAttribute ("track", formatDouble (1.0, 4));

            auto* velocity = sample->createNewChildElement ("velocity");
            velocity->setAttribute ("low", z.lowVelocity);
            velocity->setAttribute ("low-fade", 0);
            velocity->setAttribute ("high", z.highVelocity);
            velocity->setAttribute ("high-fade", 0);

            if (z.loop.enabled && ! z.oneShot)
            {
                auto* loop = sample->createNewChildElement ("loop");
                loop->setAttribute ("mode", "loop");
                loop->setAttribute ("start", formatDouble ((double) z.loop.start, 3));
                loop->setAttribute ("stop", formatDouble ((double) z.loop.end, 3));
            }
        }

        juce::XmlElement::TextFormat format;
        format.customEncoding = "UTF-8";
        format.lineWrapLength = 100000;
        return root.toString (format);
    }

    ExportResult MossBridgeExporter::writeBitwigFile (const Instrument& inst, const juce::File& file)
    {
        const auto xml = createBitwigXml (inst);

        juce::ZipFile::Builder builder;
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (xml.toRawUTF8(), xml.getNumBytesAsUTF8(), true),
                          0, "multisample.xml", juce::Time::getCurrentTime());

        for (auto& z : inst.zones)
            builder.addFile (inst.getSampleFile (z), 0, z.fileName);

        file.deleteFile();
        file.getParentDirectory().createDirectory();
        juce::FileOutputStream out (file);
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        return ExportResult::success (file);
    }

    ExportResult BitwigExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        const auto file = inst.folder.getChildFile (ExportHelpers::safeName (inst.name) + ".multisample");
        auto result = MossBridgeExporter::writeBitwigFile (inst, file);
        if (result.ok)
            result.message << "  (Bitwig: arrastra al Sampler / Studio One: arrastra a Sample One XT)";
        return result;
    }
}

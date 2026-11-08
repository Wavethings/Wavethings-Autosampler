#include "Exporter.h"
#include "Exporters.h"

namespace wt
{
    std::vector<ExporterInfo> getAvailableExporters()
    {
        std::vector<ExporterInfo> list;

        const char* nativeIds[] = { "sfz", "decentsampler", "bitwig", "ableton", "mpc", "exs", "kontakt", "sf2", "mpc-legacy" };
        for (auto* id : nativeIds)
            if (auto e = createExporter (id))
                list.push_back ({ e->getId(), e->getDisplayName(), e->getDescription(), false });

        for (auto& target : MossBridgeExporter::getTargets())
            list.push_back ({ "moss:" + target.prefix, target.displayName, target.description, true });

        return list;
    }

    std::unique_ptr<Exporter> createExporter (const juce::String& id)
    {
        if (id == "sfz")           return std::make_unique<SfzExporter>();
        if (id == "decentsampler") return std::make_unique<DecentSamplerExporter>();
        if (id == "bitwig")        return std::make_unique<BitwigExporter>();
        if (id == "ableton")       return std::make_unique<AbletonExporter>();
        if (id == "mpc")           return std::make_unique<MpcModernExporter>();
        if (id == "mpc-legacy")    return std::make_unique<MpcExporter>();
        if (id == "exs")           return std::make_unique<ExsExporter>();
        if (id == "kontakt")       return std::make_unique<KontaktExporter>();
        if (id == "sf2")           return std::make_unique<Sf2Exporter>();

        if (id.startsWith ("moss:"))
            for (auto& target : MossBridgeExporter::getTargets())
                if (target.prefix == id.fromFirstOccurrenceOf ("moss:", false, false))
                    return std::make_unique<MossBridgeExporter> (target);

        return nullptr;
    }

    namespace ExportHelpers
    {
        juce::String safeName (const juce::String& name)
        {
            auto s = juce::File::createLegalFileName (name.trim());
            s = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.#()+&,");
            return s.isEmpty() ? juce::String ("Instrument") : s;
        }

        juce::String formatDouble (double value, int decimals)
        {
            // always '.' as decimal separator, independent of the locale
            return juce::String (value, decimals, false);
        }

        juce::String escapeXml (const juce::String& text)
        {
            return text.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;").replace ("'", "&apos;");
        }

        bool copySamples (const Instrument& instrument, const juce::File& folder, juce::String& error, const juce::String& extension)
        {
            if (! folder.createDirectory())
            {
                error = "No se pudo crear " + folder.getFullPathName();
                return false;
            }

            for (auto& zone : instrument.zones)
            {
                const auto src = instrument.getSampleFile (zone);
                const auto dst = folder.getChildFile (zone.getNameWithoutExtension() + extension);
                if (! src.copyFileTo (dst))
                {
                    error = "No se pudo copiar " + src.getFullPathName();
                    return false;
                }
            }
            return true;
        }

        juce::File uniqueFile (const juce::File& folder, const juce::String& baseName, const juce::String& extension)
        {
            return folder.getChildFile (baseName + extension);
        }

        bool writeText (const juce::File& file, const juce::String& text)
        {
            file.getParentDirectory().createDirectory();
            file.deleteFile();
            juce::FileOutputStream out (file);
            if (! out.openedOk())
                return false;
            out.writeText (text, false, false, "\n");
            out.flush();
            return ! out.getStatus().failed();
        }
    }
}

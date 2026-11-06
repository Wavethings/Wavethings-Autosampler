#include "Exporters.h"
#include "../Core/Text.h"

namespace wt
{
    ExportResult SfzExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        using namespace ExportHelpers;
        juce::String s;
        const juce::String nl ("\n");

        s << "// " << inst.name << nl
          << "// Creado con Wavethings Autosampler 0.1" << nl;
        if (inst.creator.isNotEmpty())  s << "// Autor: " << inst.creator << nl;
        if (inst.category.isNotEmpty()) s << "// Categoría: "_u8 << inst.category << nl;
        s << nl;

        s << "<control>" << nl
          << "default_path=Samples/" << nl << nl;

        s << "<global>" << nl
          << "ampeg_attack=0.001" << nl
          << "ampeg_release=" << formatDouble (inst.oneShot ? 0.05 : inst.releaseSeconds, 3) << nl
          << "amp_veltrack=" << (inst.numVelocityLayers > 1 ? "60" : "100") << nl;

        if (inst.oneShot)
            s << "loop_mode=one_shot" << nl;
        s << nl;

        const auto layers = inst.getZonesByLayer();
        for (size_t l = 0; l < layers.size(); ++l)
        {
            if (layers[l].empty())
                continue;

            const auto* first = layers[l].front();
            s << "<group> // capa " << (int) l + 1 << nl
              << "lovel=" << first->lowVelocity << " hivel=" << first->highVelocity << nl << nl;

            for (auto* z : layers[l])
            {
                s << "<region> lokey=" << z->lowKey << " hikey=" << z->highKey
                  << " pitch_keycenter=" << z->rootNote;

                if (z->oneShot)
                    s << " loop_mode=one_shot";
                else if (z->loop.enabled)
                    s << " loop_mode=loop_continuous loop_start=" << (juce::int64) z->loop.start
                      << " loop_end=" << (juce::int64) z->loop.end;
                else
                    s << " loop_mode=no_loop";

                // the sample goes last: file names may contain spaces
                s << " sample=" << z->fileName << nl;
            }
            s << nl;
        }

        const auto file = inst.folder.getChildFile (safeName (inst.name) + ".sfz");
        if (! writeText (file, s))
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        return ExportResult::success (file, "usa la carpeta Samples/");
    }
}

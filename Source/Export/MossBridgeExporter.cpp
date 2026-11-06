#include "Exporters.h"
#include "../Core/Text.h"

namespace wt
{
    const std::vector<MossBridgeExporter::Target>& MossBridgeExporter::getTargets()
    {
        static const std::vector<Target> targets {
            { "Maschine",        "NI Maschine (.mxsnd)",              "Sonido para el Sampler de Maschine 2/3. Requiere ConvertWithMoss." },
            { "SXT",             "Reason NN-XT (.sxt)",               "Patch nativo de NN-XT (Reason también abre el .sfz). Requiere ConvertWithMoss."_u8 },
            { "DirectWave",      "FL DirectWave (.dwp)",       "Programa monolítico para DirectWave. Requiere ConvertWithMoss."_u8 },
            { "TX16Wx",          "TX16Wx (.txprog)",           "Sampler gratuito TX16Wx. Requiere ConvertWithMoss." },
            { "TALSampler",      "TAL-Sampler (.talsmpl)",            "Requiere ConvertWithMoss." },
            { "Renoise",         "Renoise (.xrni)",                   "Instrumento de Renoise. Requiere ConvertWithMoss." },
            { "KorgMultisample", "Korg wavestate / modwave", "Requiere ConvertWithMoss." },
            { "Deluge",          "Synthstrom Deluge",          "Requiere ConvertWithMoss." },
            { "OPXY",            "Teenage Engineering OP-XY",         "Requiere ConvertWithMoss." }
        };
        return targets;
    }

    juce::File MossBridgeExporter::findConvertWithMoss (const juce::File& configured)
    {
        if (configured.existsAsFile())
            return configured;

        // the user may have chosen the .app bundle on macOS
        if (configured.isDirectory())
        {
            const auto inBundle = configured.getChildFile ("Contents/MacOS/ConvertWithMoss");
            if (inBundle.existsAsFile())
                return inBundle;
        }

        juce::StringArray candidates;
       #if JUCE_MAC
        candidates.add ("/Applications/ConvertWithMoss.app/Contents/MacOS/ConvertWithMoss");
        candidates.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                            .getChildFile ("Applications/ConvertWithMoss.app/Contents/MacOS/ConvertWithMoss").getFullPathName());
       #elif JUCE_WINDOWS
        for (auto base : { juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory),
                           juce::File::getSpecialLocation (juce::File::globalApplicationsDirectoryX86),
                           juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getSiblingFile ("Local").getChildFile ("Programs") })
        {
            candidates.add (base.getChildFile ("ConvertWithMoss/ConvertWithMossCLI.exe").getFullPathName());
            candidates.add (base.getChildFile ("ConvertWithMoss/ConvertWithMoss.exe").getFullPathName());
        }
       #else
        candidates.add ("/opt/convertwithmoss/bin/ConvertWithMoss");
        candidates.add ("/usr/bin/convertwithmoss");
        candidates.add ("/usr/local/bin/convertwithmoss");
        candidates.add ("/usr/bin/ConvertWithMoss");
       #endif

        for (auto& c : candidates)
            if (juce::File (c).existsAsFile())
                return juce::File (c);

        return {};
    }

    ExportResult MossBridgeExporter::exportInstrument (const Instrument& inst, const ExportOptions& options)
    {
        const auto exe = findConvertWithMoss (options.convertWithMossExecutable);
        if (! exe.existsAsFile())
            return ExportResult::failure ("ConvertWithMoss no encontrado. Instálalo (github.com/git-moss/ConvertWithMoss) y selecciona su ejecutable en Exportar > ConvertWithMoss."_u8);

        // hand-over file
        const auto temp = inst.folder.getChildFile (".moss-temp");
        temp.deleteRecursively();
        const auto source = temp.getChildFile (ExportHelpers::safeName (inst.name) + ".multisample");
        const auto bitwig = writeBitwigFile (inst, source);
        if (! bitwig.ok)
            return bitwig;

        const auto destination = inst.folder.getChildFile (target.prefix);
        destination.createDirectory();

        juce::StringArray args { exe.getFullPathName(), "-s", "Bitwig", "-d", target.prefix, "-f",
                                 source.getFullPathName(), destination.getFullPathName() };

        juce::ChildProcess process;
        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            temp.deleteRecursively();
            return ExportResult::failure ("No se pudo ejecutar " + exe.getFullPathName());
        }

        const auto output = process.readAllProcessOutput();
        process.waitForProcessToFinish (10 * 60 * 1000);
        const auto exitCode = process.getExitCode();
        temp.deleteRecursively();

        if (options.log)
            for (auto& line : juce::StringArray::fromLines (output))
                if (line.trim().isNotEmpty())
                    options.log ("  [ConvertWithMoss] " + line.trim());

        const bool hasFiles = ! destination.findChildFiles (juce::File::findFiles, true).isEmpty();
        if (exitCode != 0 || ! hasFiles)
            return ExportResult::failure ("ConvertWithMoss no generó archivos (código "_u8 + juce::String ((int) exitCode) + ")");

        return ExportResult::success (destination, "convertido con ConvertWithMoss");
    }
}

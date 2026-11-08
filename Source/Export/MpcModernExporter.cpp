#include "Exporters.h"
#include <map>
#include "../Core/Text.h"
#include "BinaryData.h"
#include "../Core/WavFile.h"

// MPC 3.x keygroup program: a gzip file holding a short binary header ("ACVS...") followed by a JSON
// document. As in Wavethings Keygroup Creator, a program saved by the MPC itself is used as the
// template, and only the keygroups, layers, samples and a few sound settings are replaced.
// The samples go into "<name>_[ProgramData]/" next to the .xpm; the program stores only file names.

namespace wt
{
    namespace
    {
        juce::MemoryBlock& templateOverride() { static juce::MemoryBlock m; return m; }

        juce::MemoryBlock getTemplateBytes()
        {
            if (templateOverride().getSize() > 0)
                return templateOverride();

            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                if (juce::String (BinaryData::originalFilenames[i]).endsWithIgnoreCase (".xpm"))
                {
                    int size = 0;
                    if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                        return { data, (size_t) size };
                }
            }
            return {};
        }

        bool readProgram (const juce::MemoryBlock& file, juce::MemoryBlock& header, juce::var& json)
        {
            juce::MemoryBlock raw;
            const auto* bytes = static_cast<const juce::uint8*> (file.getData());

            if (file.getSize() > 2 && bytes[0] == 0x1f && bytes[1] == 0x8b)
            {
                juce::MemoryInputStream in (file, false);
                juce::GZIPDecompressorInputStream gz (&in, false, juce::GZIPDecompressorInputStream::gzipFormat);
                gz.readIntoMemoryBlock (raw);
            }
            else
            {
                raw = file;
            }

            const auto* text = static_cast<const char*> (raw.getData());
            size_t brace = 0;
            while (brace < raw.getSize() && text[brace] != '{')
                ++brace;

            if (brace >= raw.getSize() || raw.getSize() < 4 || std::memcmp (text, "ACVS", 4) != 0)
                return false;

            header = juce::MemoryBlock (text, brace);
            json = juce::JSON::parse (juce::String::fromUTF8 (text + brace, (int) (raw.getSize() - brace)));
            return json.isObject();
        }

        juce::MemoryBlock writeProgram (const juce::MemoryBlock& header, const juce::var& json)
        {
            juce::MemoryOutputStream raw;
            raw.write (header.getData(), header.getSize());
            const auto text = juce::JSON::toString (json, juce::JSON::FormatOptions{}.withSpacing (juce::JSON::Spacing::multiLine)
                                                                                         .withIndentLevel (0));
            raw.write (text.toRawUTF8(), text.getNumBytesAsUTF8());

            juce::MemoryOutputStream gzData;
            {
                juce::GZIPCompressorOutputStream gz (gzData, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
                gz.write (raw.getData(), raw.getDataSize());
                gz.flush();
            }

            auto block = gzData.getMemoryBlock();
            if (block.getSize() > 9)
                static_cast<juce::uint8*> (block.getData())[9] = 0x13;   // same OS byte the MPC writes
            return block;
        }

        juce::var get (const juce::var& v, const juce::Identifier& key) { return v.getProperty (key, {}); }
        void set (const juce::var& v, const juce::Identifier& key, const juce::var& value)
        {
            if (auto* o = v.getDynamicObject())
                o->setProperty (key, value);
        }

        double envTime (double seconds)
        {
            // Measured on an MPC 3.x (see Wavethings Keygroup Creator):
            // time = 30000 ^ value ms, value 0..1 (0 -> 0 ms, 1 -> 30 s)
            const double ms = seconds * 1000.0;
            if (ms < 1.0)
                return 0.0;
            return juce::jlimit (0.0, 1.0, std::log (ms) / std::log (30000.0));
        }
    }

    void MpcModernExporter::setTemplateForTests (const juce::MemoryBlock& data) { templateOverride() = data; }

    juce::MemoryBlock MpcModernExporter::makeAtemChunk (const SampleZone& z, bool loop)
    {
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double seconds = (double) z.numFrames / z.sampleRate;
        const int bars = juce::jmax (1, juce::roundToInt (seconds / 2.0));   // 4/4 at 120 BPM

        juce::String j;
        const juce::String nl ("\n");
        j << "{" << nl
          << "    \"version\": 1," << nl
          << "    \"value0\": {" << nl
          << "        \"defaultSlice\": {" << nl
          << "            \"Start\": 0," << nl
          << "            \"End\": " << (loop ? z.loop.end : z.numFrames - 1) << "," << nl
          << "            \"LoopStart\": " << (loop ? z.loop.start : (juce::int64) 0) << "," << nl
          << "            \"LoopMode\": " << (loop ? 1 : 0) << "," << nl
          << "            \"PulsePosition\": 0," << nl
          << "            \"LoopCrossfadeLength\": " << (loop ? z.loop.crossfadeFrames : 0) << "," << nl
          << "            \"LoopCrossfadeType\": " << (z.loop.crossfadeLinear ? 0 : 1) << "," << nl
          << "            \"TailLength\": 0.0," << nl
          << "            \"TailLoopPosition\": 0.5," << nl
          << "            \"NumLoopRepeats\": 0" << nl
          << "        }," << nl
          << "        \"numBars\": " << bars << "," << nl
          << "        \"Num slices\": 0" << nl
          << "    }," << nl
          << "    \"value1\": {" << nl
          << "        \"version\": 1," << nl
          << "        \"note\": \"" << noteNames[z.rootNote % 12] << "\"," << nl
          << "        \"scale\": \"Major\"" << nl
          << "    }" << nl
          << "}";
        // keep the chunk size even, like the MPC does (no RIFF pad byte needed)
        if (j.getNumBytesAsUTF8() % 2 != 0)
            j << nl;
        return { j.toRawUTF8(), j.getNumBytesAsUTF8() };
    }

    juce::MemoryBlock MpcModernExporter::makeSmplChunk (const SampleZone& z, bool loop)
    {
        // 'smpl' as written by the MPC: Akai manufacturer id, one loop, and 34 extra bytes after the loop
        // where the MPC keeps the end point and the loop crossfade (length + type) that it shows as X-FADE.
        const juce::int64 end = loop ? z.loop.end : z.numFrames - 1;
        const double seconds = (double) z.numFrames / z.sampleRate;
        const int bars = juce::jmax (1, juce::roundToInt (seconds / 2.0));

        juce::MemoryOutputStream m;
        m.writeInt (0x01000047);                                    // manufacturer: Akai
        m.writeInt (94);                                            // product (as the MPC writes it)
        m.writeInt ((int) std::round (1.0e9 / z.sampleRate));       // sample period
        m.writeInt (z.rootNote);
        m.writeInt (0);                                             // pitch fraction
        m.writeInt (25);                                            // SMPTE format (as the MPC writes it)
        m.writeInt (0);                                             // SMPTE offset
        m.writeInt (1);                                             // one loop
        m.writeInt (0);                                             // sampler data (declared)

        m.writeInt (0);                                             // cue id
        m.writeInt (0);                                             // forward
        m.writeInt ((int) (loop ? z.loop.start : 0));
        m.writeInt ((int) end);
        m.writeInt (0);
        m.writeInt (0);

        juce::uint8 ext[34] = {};
        ext[0] = 3;                                                 // version
        ext[2] = (juce::uint8) z.rootNote;
        ext[6] = (juce::uint8) juce::jmin (255, bars);
        auto put32 = [&] (int offset, juce::uint32 v) { for (int i = 0; i < 4; ++i) ext[offset + i] = (juce::uint8) ((v >> (8 * i)) & 0xff); };
        put32 (14, (juce::uint32) end);
        put32 (20, (juce::uint32) (loop ? z.loop.crossfadeFrames : 0));
        ext[24] = z.loop.crossfadeLinear ? 0 : 1;                   // 0 = linear, 1 = curved
        ext[33] = 0x3f;
        m.write (ext, sizeof (ext));
        return m.getMemoryBlock();
    }

    bool MpcModernExporter::hasTemplate()
    {
        return getTemplateBytes().getSize() > 0;
    }

    ExportResult MpcModernExporter::exportInstrument (const Instrument& inst, const ExportOptions& options)
    {
        juce::MemoryBlock header;
        juce::var doc;
        if (! readProgram (getTemplateBytes(), header, doc))
        {
            if (options.log)
                options.log ("MPC 3.x: falta la plantilla de la MPC; se usa el formato antiguo (legacy)");
            MpcExporter legacy;
            return legacy.exportInstrument (inst, options);
        }

        const auto safe = ExportHelpers::safeName (inst.name);
        const auto folder = inst.folder.getChildFile ("MPC");
        const auto dataFolder = folder.getChildFile (safe + "_[ProgramData]");

        // The MPC keeps the loop (and its crossfade) of every sample in an 'atem' chunk inside the WAV,
        // a small JSON document. The program refers to it, but the values in the sample win.
        dataFolder.createDirectory();
        for (auto& z : inst.zones)
        {
            const bool loop = z.loop.enabled && ! z.oneShot;
            const auto atem = makeAtemChunk (z, loop);
            juce::MemoryBlock tempo;
            {
                juce::MemoryOutputStream m (tempo, false);
                m.writeInt (120000);   // 'meta': tempo x 1000, as written by the MPC
            }
            // same layout as a WAV saved by the MPC: fmt, atem, smpl, meta, data
            if (! WavFile::copyWithExtraChunks (inst.getSampleFile (z), dataFolder.getChildFile (z.fileName),
                                                { { "atem", atem }, { "smpl", makeSmplChunk (z, loop) } }, { { "meta", tempo } }))
                return ExportResult::failure ("No se pudo copiar " + z.fileName);
        }

        auto data = get (doc, "data");
        auto drum = get (data, "drum");
        auto templateInstruments = get (drum, "instruments");
        if (! templateInstruments.isArray() || templateInstruments.size() == 0)
            return ExportResult::failure ("Plantilla MPC no válida"_u8);

        set (data, "name", inst.name);

        // prototypes: an instrument, a layer with a sample and an empty layer
        const auto proto = templateInstruments[0].clone();
        const auto protoLayers = get (proto, "layersv");
        juce::var fullLayer, emptyLayer;
        for (int i = 0; i < protoLayers.size(); ++i)
        {
            const auto layer = protoLayers[i];
            if (get (layer, "sampleName").toString().isNotEmpty()) { if (fullLayer.isVoid()) fullLayer = layer; }
            else if (emptyLayer.isVoid()) emptyLayer = layer;
        }
        if (fullLayer.isVoid()) fullLayer = protoLayers[0];
        if (emptyLayer.isVoid())
        {
            emptyLayer = fullLayer.clone();
            set (emptyLayer, "sampleName", "");
            set (emptyLayer, "sampleFile", "");
            set (emptyLayer, "active", false);
        }

        const int numLayers = juce::jmax (8, protoLayers.size());
        const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;

        auto applySound = [&] (const juce::var& synthSection)
        {
            auto amp = get (synthSection, "ampEnvelope");
            auto setEnv = [&] (const char* key, const juce::var& value)
            {
                auto e = get (amp, key);
                if (e.isObject()) set (e, "value0", value);
            };
            setEnv ("Attack", 0.0);
            setEnv ("Hold", 0.0);
            setEnv ("Sustain", 1.0);
            setEnv ("Release", envTime (release));
            setEnv ("OneShot", inst.oneShot);
            if (synthSection.hasProperty ("velocitySensitivity"))
                set (synthSection, "velocitySensitivity", inst.numVelocityLayers > 1 ? 0.35 : 1.0);
        };

        // one keygroup per key range; the velocity layers become its layers (max. 8)
        std::map<std::pair<int, int>, std::vector<const SampleZone*>> keygroups;
        for (auto& z : inst.zones)
            keygroups[{ z.lowKey, z.highKey }].push_back (&z);

        juce::Array<juce::var> instruments;
        juce::Random random (12345);
        std::map<juce::String, int> usedSamples;

        for (auto& [range, zones] : keygroups)
        {
            std::sort (zones.begin(), zones.end(), [] (auto* a, auto* b) { return a->lowVelocity < b->lowVelocity; });
            if (zones.size() > 8)
                zones.erase (zones.begin(), zones.end() - 8);

            auto kg = proto.clone();
            set (kg, "lowNote", range.first);
            set (kg, "highNote", range.second);
            set (kg, "randomPlaySeed", random.nextInt ({ 1, 999999 }));
            set (kg, "tempo", 120.0);
            if (kg.hasProperty ("triggerMode"))
                set (kg, "triggerMode", inst.oneShot ? 0 : 2);   // 0 = one shot, 2 = note on
            applySound (get (kg, "synthSection"));

            const int layerSeed = random.nextInt ({ 1, 999999 });
            juce::Array<juce::var> layers;

            for (int i = 0; i < numLayers; ++i)
            {
                juce::var layer;
                if (i < (int) zones.size())
                {
                    const auto& z = *zones[(size_t) i];
                    const bool loop = z.loop.enabled && ! z.oneShot;
                    layer = fullLayer.clone();
                    set (layer, "active", true);
                    set (layer, "mute", false);
                    set (layer, "velocityStart", z.lowVelocity <= 1 ? 0 : z.lowVelocity);   // the MPC uses 0-127
                    set (layer, "velocityEnd", z.highVelocity);
                    set (layer, "rootNote", z.rootNote + 1);
                    set (layer, "keyTrackEnable", true);
                    set (layer, "sampleName", z.getNameWithoutExtension());
                    set (layer, "sampleFile", z.fileName);
                    set (layer, "sampleStart", 0);
                    set (layer, "sampleEnd", 0);
                    set (layer, "loop", false);
                    set (layer, "loopStart", 0);
                    set (layer, "loopEnd", 0);

                    auto slice = get (layer, "sliceInfo");
                    set (slice, "Start", 0);
                    // last sample that plays (an index inside the file, never past its end)
                    set (slice, "End", loop ? (juce::int64) z.loop.end : z.numFrames - 1);
                    set (slice, "LoopStart", loop ? (juce::int64) z.loop.start : (juce::int64) 0);
                    set (slice, "LoopMode", loop ? 1 : 0);
                    // the loop crossfade of the MPC (in samples), only when it is not rendered into the WAV
                    set (slice, "LoopCrossfadeLength", loop ? z.loop.crossfadeFrames : 0);
                    set (slice, "LoopCrossfadeType", z.loop.crossfadeLinear ? 0 : 1);
                    set (layer, "loopCrossfadeLength", loop ? z.loop.crossfadeFrames : 0);

                    usedSamples.emplace (z.fileName, z.rootNote);
                }
                else
                {
                    layer = emptyLayer.clone();
                }

                if (layer.hasProperty ("sliceIncrementRngSeed"))
                    set (layer, "sliceIncrementRngSeed", layerSeed);
                layers.add (layer);
            }

            set (kg, "layersv", layers);
            instruments.add (kg);
        }

        set (drum, "instruments", instruments);
        auto keygroup = get (data, "keygroup");
        set (keygroup, "numKeygroups", instruments.size());
        applySound (get (keygroup, "synthSection"));

        // sample list
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        auto templateSamples = get (data, "samples");
        juce::Array<juce::var> samples;
        for (auto& [file, root] : usedSamples)
        {
            juce::var entry;
            if (templateSamples.isArray() && templateSamples.size() > 0)
                entry = templateSamples[0].clone();
            else
            {
                entry = new juce::DynamicObject();
                set (entry, "version", 1);
                set (entry, "loadImpl", 0);
                set (entry, "metadata", new juce::DynamicObject());
            }
            set (entry, "name", file.upToLastOccurrenceOf (".", false, false));
            set (entry, "path", file);
            auto meta = get (entry, "metadata");
            if (meta.isObject())
            {
                set (meta, "tempo", 120.0);
                set (meta, "rootNote", root);
                set (meta, "tune", 0.0);
                set (meta, "key", juce::String (noteNames[root % 12]) + " Major");
            }
            samples.add (entry);
        }
        set (data, "samples", samples);

        const auto file = folder.getChildFile (safe + ".xpm");
        file.getParentDirectory().createDirectory();
        const auto bytes = writeProgram (header, doc);
        if (! file.replaceWithData (bytes.getData(), bytes.getSize()))
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        if (inst.numVelocityLayers > 8 && options.log)
            options.log ("MPC: solo 8 capas por keygroup; se exportan las 8 más fuertes"_u8);

        return ExportResult::success (file, "copia el .xpm y su carpeta _[ProgramData] a la MPC");
    }
}

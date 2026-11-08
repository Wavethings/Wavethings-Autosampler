#include "Exporters.h"
#include <map>
#include "../Core/Text.h"
#include "../Core/WavFile.h"

// Binary layout of the EXS24 blocks as documented by the EXS writer of
// ConvertWithMoss (LGPL-3.0, (c) Jürgen Moßgraber).

namespace wt
{
    namespace
    {
        constexpr int typeInstrument = 0x00, typeZone = 0x01, typeGroup = 0x02, typeSample = 0x03, typeParams = 0x04;

        struct BigEndianWriter
        {
            juce::MemoryOutputStream out;

            void u8 (int v)  { out.writeByte ((char) (v & 0xff)); }
            void s8 (int v)  { u8 (v < 0 ? v + 256 : v); }
            void u16 (int v) { out.writeShortBigEndian ((short) (v & 0xffff)); }
            void u32 (juce::int64 v) { out.writeIntBigEndian ((int) (juce::uint32) (v & 0xffffffff)); }
            void pad (int n) { for (int i = 0; i < n; ++i) out.writeByte (0); }
            void ascii (const juce::String& s, int length)
            {
                juce::HeapBlock<char> buffer ((size_t) length, true);
                const auto text = s.toStdString();
                std::memcpy (buffer.get(), text.data(), (size_t) juce::jmin ((int) text.size(), length - 1));
                out.write (buffer.get(), (size_t) length);
            }
            void block (const juce::MemoryBlock& data) { out.write (data.getData(), data.getSize()); }
        };

        juce::String asciiOnly (const juce::String& s)
        {
            juce::String r;
            for (auto c : s)
                r << (c >= 32 && c < 127 ? juce::String::charToString (c) : juce::String ("_"));
            return r;
        }

        void writeBlock (juce::OutputStream& file, int type, int index, const juce::String& name, const juce::MemoryBlock& content)
        {
            BigEndianWriter w;
            w.u8 (0);          // big endian
            w.u8 (1);          // version
            w.u8 (0);
            w.u8 (type);
            w.u32 ((juce::int64) content.getSize());
            w.u32 (index);
            w.u32 (0);
            w.out.write ("SOBT", 4);
            w.ascii (asciiOnly (name), 64);
            w.block (content);
            file.write (w.out.getData(), w.out.getDataSize());
        }

        int envTime (double seconds)
        {
            if (seconds <= 0.0)
                return 0;
            return juce::roundToInt (127.0 * std::pow (juce::jmin (seconds, 10.0) / 10.0, 0.25));
        }
    }

    ExportResult ExsExporter::exportInstrument (const Instrument& inst, const ExportOptions&)
    {
        const auto safe = ExportHelpers::safeName (inst.name);
        const auto folder = inst.folder.getChildFile ("Logic").getChildFile (safe);

        juce::String error;
        if (! ExportHelpers::copySamples (inst, folder, error))
            return ExportResult::failure (error);

        const auto file = folder.getChildFile (safe + ".exs");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        const auto layers = inst.getZonesByLayer();
        std::vector<int> layerToGroup (layers.size(), 0);
        int numGroups = 0;
        for (size_t l = 0; l < layers.size(); ++l)
            if (! layers[l].empty())
                layerToGroup[l] = numGroups++;

        // Instrument header ------------------------------------------------------------
        {
            BigEndianWriter w;
            w.u32 (0);
            w.u32 ((juce::int64) inst.zones.size());
            w.u32 (numGroups);
            w.u32 ((juce::int64) inst.zones.size());
            w.u32 (1);
            for (int i = 0; i < 5; ++i) w.u32 (0);
            writeBlock (out, typeInstrument, 0, inst.name, w.out.getMemoryBlock());
        }

        // Zones ------------------------------------------------------------------------
        for (size_t i = 0; i < inst.zones.size(); ++i)
        {
            const auto& z = inst.zones[i];
            const bool loop = z.loop.enabled && ! z.oneShot;

            BigEndianWriter w;
            w.u8 ((z.oneShot ? 1 : 0) | 0 /* pitch on */ | 8 /* velocity range on */);
            w.u8 (z.rootNote);
            w.s8 (0);             // fine tune
            w.s8 (0);             // pan
            w.s8 (juce::roundToInt (z.gainDb));
            w.u8 (0);             // volume scale
            w.u8 (z.lowKey);
            w.u8 (z.highKey);
            w.pad (1);
            w.u8 (z.lowVelocity);
            w.u8 (z.highVelocity);
            w.pad (1);
            w.u32 (0);                                  // sample start
            w.u32 (z.numFrames);                        // sample end
            w.u32 (loop ? z.loop.start : 0);
            w.u32 (loop ? z.loop.end + 1 : 0);          // EXS loop end is exclusive
            w.u32 (loop ? juce::roundToInt (z.loop.crossfadeFrames * 1000.0 / z.sampleRate) : 0);   // crossfade in ms
            w.u8 (0);                                   // loop tune
            w.u8 (loop ? 1 : 0);                        // loop on
            w.u8 (0);                                   // direction forward
            w.pad (42);
            w.u8 (0); w.u8 (0); w.u8 (0);               // flex options, flex speed, tail tune
            w.s8 (0);                                   // coarse tune
            w.pad (1);
            w.u8 (0);                                   // output
            w.pad (5);
            w.u32 (layerToGroup[(size_t) juce::jlimit (0, (int) layers.size() - 1, z.layerIndex)]);
            w.u32 ((juce::int64) i);                    // sample index
            writeBlock (out, typeZone, (int) i, z.getNameWithoutExtension(), w.out.getMemoryBlock());
        }

        // Groups (one per velocity layer) --------------------------------------------------
        for (size_t l = 0; l < layers.size(); ++l)
        {
            if (layers[l].empty())
                continue;

            BigEndianWriter w;
            w.s8 (0);  w.u8 (0);  w.u8 (0);  w.u8 (0);  // volume, pan, polyphony, options
            w.u8 (0);                                   // exclusive
            w.u8 (0);  w.u8 (127);                      // min/max velocity (zones carry the ranges)
            w.u8 (0);                                   // sample select random offset
            w.pad (8);
            w.u16 (0);                                  // release trigger time
            w.pad (14);
            w.u8 (128); w.u8 (0); w.u8 (0); w.u8 (128); // velocity/key crossfades
            w.pad (2);
            w.u8 (80); w.u8 (140);                      // enable by tempo low/high
            w.pad (1); w.u8 (0); w.pad (1); w.u8 (0);   // cutoff / reso offsets
            w.pad (12);
            for (int i = 0; i < 4; ++i) w.u32 (0);      // envelope offsets
            w.pad (1);
            w.u8 (0);                                   // release trigger
            w.u8 (0);                                   // output
            w.u8 (0);                                   // enable by note value
            w.pad (4);
            w.u32 (0xffffffff);                         // round robin position (-1)
            w.u8 (0); w.u8 (0); w.u8 (0); w.u8 (127);   // enable by type / control
            w.u8 (0); w.u8 (127);                       // start / end note
            w.u8 (0); w.u8 (0);                         // midi channel, articulation

            const auto name = "Velocidad " + juce::String (layers[l].front()->lowVelocity) + "-" + juce::String (layers[l].front()->highVelocity);
            writeBlock (out, typeGroup, layerToGroup[l], name, w.out.getMemoryBlock());
        }

        // Samples ----------------------------------------------------------------------
        for (size_t i = 0; i < inst.zones.size(); ++i)
        {
            const auto& z = inst.zones[i];
            const auto sampleFile = folder.getChildFile (z.fileName);
            WavFile::Info info;
            if (! WavFile::readInfo (sampleFile, info))
                return ExportResult::failure ("WAV no válido: "_u8 + sampleFile.getFullPathName());

            BigEndianWriter w;
            w.u32 (info.dataOffset);
            w.u32 (info.numFrames);
            w.u32 ((juce::int64) info.sampleRate);
            w.u32 (info.bitsPerSample);
            w.u32 (info.numChannels);
            w.u32 (info.numChannels);
            w.pad (4);
            w.out.write ("WAVE", 4);
            w.u32 (sampleFile.getSize());
            w.u32 (0);                                  // not compressed
            w.pad (40);
            w.ascii ("", 256);                          // path: Logic looks next to the .exs file
            w.ascii (asciiOnly (z.fileName), 256);
            writeBlock (out, typeSample, (int) i, z.fileName, w.out.getMemoryBlock());
        }

        // Parameters -------------------------------------------------------------------
        {
            const int velocitySensitivity = inst.numVelocityLayers > 1 ? -21 : -60;
            const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;

            std::map<int, int> params {
                { 0x03, 2 },                    // pitch bend up
                { 0x04, 2 },                    // pitch bend down
                { 0x2c, 0 },                    // filter off
                { 0x51, 127 },                  // env1 sustain
                { 0x52, 0 },                    // env1 attack (hi vel)
                { 0x53, 0 },                    // env1 attack (lo vel)
                { 0x54, 0 },                    // env1 decay
                { 0x55, envTime (release) },    // env1 release
                { 0x58, 0 },                    // env1 hold
                { 0x5a, velocitySensitivity },  // env1 velocity sensitivity
                { 0x16d, 0 }                    // env1 delay
            };

            BigEndianWriter ids, values, extended;
            int written = 0;
            for (auto& [id, value] : params)
                if (id <= 0xff) { ids.u8 (id); values.u16 (value); ++written; }
            for (; written < 100; ++written) { ids.u8 (0); values.u16 (0); }

            written = 0;
            for (auto& [id, value] : params)
                if (id > 0xff) { extended.u16 (id); extended.u16 (value); ++written; }
            for (; written < 200; ++written) { extended.u16 (0); extended.u16 (0); }

            BigEndianWriter w;
            w.u32 (100);
            w.block (ids.out.getMemoryBlock());
            w.block (values.out.getMemoryBlock());
            w.u32 (200);
            w.block (extended.out.getMemoryBlock());
            writeBlock (out, typeParams, 0, "", w.out.getMemoryBlock());
        }

        out.flush();
        return ExportResult::success (file, "abre el .exs desde el Sampler de Logic");
    }
}

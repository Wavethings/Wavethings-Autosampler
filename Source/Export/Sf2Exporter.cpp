#include "Exporters.h"
#include "../Core/Text.h"
#include "../Core/WavFile.h"

// SoundFont 2.04 writer (16 bit 'smpl' + 8 bit 'sm24' for 24 bit sources).

namespace wt
{
    namespace
    {
        enum Generator
        {
            genPan = 17,
            genReleaseVolEnv = 38,
            genInstrument = 41,
            genKeyRange = 43,
            genVelRange = 44,
            genSampleID = 53,
            genSampleModes = 54,
            genOverridingRootKey = 58
        };

        void writeFixedString (juce::MemoryOutputStream& out, const juce::String& s, int length)
        {
            juce::HeapBlock<char> buffer ((size_t) length, true);
            int n = 0;
            for (auto c : s)
            {
                if (n >= length - 1) break;
                buffer[n++] = (c >= 32 && c < 127) ? (char) c : '_';
            }
            out.write (buffer.get(), (size_t) length);
        }

        void writeChunk (juce::MemoryOutputStream& out, const char* id, const juce::MemoryBlock& data)
        {
            out.write (id, 4);
            out.writeInt ((int) data.getSize());
            out.write (data.getData(), data.getSize());
            if (data.getSize() & 1)
                out.writeByte (0);
        }

        void writeInfoString (juce::MemoryOutputStream& out, const char* id, const juce::String& text)
        {
            juce::MemoryOutputStream s;
            juce::String clean;
            for (auto c : text)
                clean << ((c >= 32 && c < 127) ? juce::String::charToString (c) : juce::String ("_"));
            s.write (clean.toRawUTF8(), (size_t) juce::jmin (255, clean.length()));
            s.writeByte (0);
            if (s.getDataSize() & 1)
                s.writeByte (0);
            writeChunk (out, id, s.getMemoryBlock());
        }

        juce::MemoryBlock list (const char* type, const juce::MemoryBlock& content)
        {
            juce::MemoryOutputStream out;
            out.write (type, 4);
            out.write (content.getData(), content.getSize());
            return out.getMemoryBlock();
        }

        struct Gen { int op; int amount; };

        void writeGen (juce::MemoryOutputStream& out, const Gen& g)
        {
            out.writeShort ((short) g.op);
            out.writeShort ((short) g.amount);
        }

        int range (int lo, int hi) { return (lo & 0xff) | ((hi & 0xff) << 8); }
    }

    ExportResult Sf2Exporter::exportInstrument (const Instrument& inst, const ExportOptions& options)
    {
        const bool use24Bit = inst.bitsPerSample >= 24;

        juce::MemoryOutputStream smpl, sm24, shdr, igen, ibag;
        struct SampleHeader { juce::String name; juce::int64 start, end, loopStart, loopEnd; int rate, root, link, type; };
        std::vector<SampleHeader> headers;

        struct ZoneSamples { const SampleZone* zone; std::vector<int> sampleIds; };
        std::vector<ZoneSamples> zoneSamples;

        juce::int64 position = 0;

        for (auto& z : inst.zones)
        {
            juce::AudioBuffer<float> audio;
            WavFile::Info info;
            if (! WavFile::read (inst.getSampleFile (z), audio, info))
                return ExportResult::failure ("No se pudo leer " + z.fileName);

            const int numChannels = juce::jmin (2, audio.getNumChannels());
            ZoneSamples zs { &z, {} };

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* d = audio.getReadPointer (ch);
                const int n = audio.getNumSamples();

                for (int i = 0; i < n; ++i)
                {
                    const int v = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, d[i]) * 8388607.0f);
                    smpl.writeShort ((short) (v >> 8));
                    if (use24Bit)
                        sm24.writeByte ((char) (v & 0xff));
                }
                for (int i = 0; i < 46; ++i)
                {
                    smpl.writeShort (0);
                    if (use24Bit) sm24.writeByte (0);
                }

                SampleHeader h;
                h.name = z.getNameWithoutExtension().substring (0, numChannels > 1 ? 17 : 19) + (numChannels > 1 ? (ch == 0 ? "_L" : "_R") : "");
                h.start = position;
                h.end = position + n;
                const bool loop = z.loop.enabled && ! z.oneShot;
                h.loopStart = position + (loop ? z.loop.start : juce::jmin<juce::int64> (8, n));
                h.loopEnd = position + (loop ? z.loop.end + 1 : juce::jmax<juce::int64> (8, n - 8));
                h.rate = (int) z.sampleRate;
                h.root = z.rootNote;
                h.type = numChannels == 1 ? 1 : (ch == 0 ? 4 : 2);
                h.link = 0;
                position += n + 46;

                zs.sampleIds.push_back ((int) headers.size());
                headers.push_back (h);
            }

            if (zs.sampleIds.size() == 2)
            {
                headers[(size_t) zs.sampleIds[0]].link = zs.sampleIds[1];
                headers[(size_t) zs.sampleIds[1]].link = zs.sampleIds[0];
            }

            zoneSamples.push_back (zs);
        }

        if (position > 0x7fffffff / 2 && options.log)
            options.log ("SF2: el archivo es muy grande; algunos reproductores no lo cargarán"_u8);

        // Instrument zones --------------------------------------------------------------
        const double release = inst.oneShot ? 0.05 : inst.releaseSeconds;
        const int releaseTimecents = juce::jlimit (-12000, 8000, juce::roundToInt (1200.0 * std::log2 (juce::jmax (0.001, release))));

        int genIndex = 0, bagIndex = 0;
        for (auto& zs : zoneSamples)
        {
            for (size_t c = 0; c < zs.sampleIds.size(); ++c)
            {
                const auto& z = *zs.zone;
                std::vector<Gen> gens {
                    { genKeyRange, range (z.lowKey, z.highKey) },
                    { genVelRange, range (z.lowVelocity, z.highVelocity) },
                };
                if (zs.sampleIds.size() == 2)
                    gens.push_back ({ genPan, c == 0 ? -500 : 500 });
                gens.push_back ({ genReleaseVolEnv, releaseTimecents });
                gens.push_back ({ genSampleModes, (z.loop.enabled && ! z.oneShot) ? 1 : 0 });
                gens.push_back ({ genOverridingRootKey, z.rootNote });
                gens.push_back ({ genSampleID, zs.sampleIds[c] });

                ibag.writeShort ((short) genIndex);
                ibag.writeShort (0);
                for (auto& g : gens)
                    writeGen (igen, g);
                genIndex += (int) gens.size();
                ++bagIndex;
            }
        }

        // terminal records
        ibag.writeShort ((short) genIndex);
        ibag.writeShort (0);
        writeGen (igen, { 0, 0 });

        juce::MemoryOutputStream instChunk;
        writeFixedString (instChunk, inst.name, 20);
        instChunk.writeShort (0);
        writeFixedString (instChunk, "EOI", 20);
        instChunk.writeShort ((short) bagIndex);

        juce::MemoryOutputStream imod;
        for (int i = 0; i < 10; ++i) imod.writeByte (0);

        // Preset ---------------------------------------------------------------------------
        juce::MemoryOutputStream phdr, pbag, pmod, pgen;
        writeFixedString (phdr, inst.name, 20);
        phdr.writeShort (0);    // preset number
        phdr.writeShort (0);    // bank
        phdr.writeShort (0);    // bag index
        phdr.writeInt (0); phdr.writeInt (0); phdr.writeInt (0);
        writeFixedString (phdr, "EOP", 20);
        phdr.writeShort (0); phdr.writeShort (0);
        phdr.writeShort (1);
        phdr.writeInt (0); phdr.writeInt (0); phdr.writeInt (0);

        pbag.writeShort (0); pbag.writeShort (0);
        pbag.writeShort (1); pbag.writeShort (0);
        for (int i = 0; i < 10; ++i) pmod.writeByte (0);
        writeGen (pgen, { genInstrument, 0 });
        writeGen (pgen, { 0, 0 });

        // Sample headers -----------------------------------------------------------------------
        for (auto& h : headers)
        {
            writeFixedString (shdr, h.name, 20);
            shdr.writeInt ((int) h.start);
            shdr.writeInt ((int) h.end);
            shdr.writeInt ((int) h.loopStart);
            shdr.writeInt ((int) h.loopEnd);
            shdr.writeInt (h.rate);
            shdr.writeByte ((char) h.root);
            shdr.writeByte (0);
            shdr.writeShort ((short) h.link);
            shdr.writeShort ((short) h.type);
        }
        writeFixedString (shdr, "EOS", 20);
        for (int i = 0; i < 26; ++i) shdr.writeByte (0);

        // Assemble -------------------------------------------------------------------------------
        juce::MemoryOutputStream info;
        {
            juce::MemoryOutputStream ifil;
            ifil.writeShort (2);
            ifil.writeShort (use24Bit ? 4 : 1);
            writeChunk (info, "ifil", ifil.getMemoryBlock());
            writeInfoString (info, "isng", "EMU8000");
            writeInfoString (info, "INAM", inst.name);
            if (inst.creator.isNotEmpty())
                writeInfoString (info, "IENG", inst.creator);
            writeInfoString (info, "ISFT", "Wavethings Autosampler 0.1");
            if (inst.description.isNotEmpty())
                writeInfoString (info, "ICMT", inst.description);
        }

        juce::MemoryOutputStream sdta;
        writeChunk (sdta, "smpl", smpl.getMemoryBlock());
        if (use24Bit)
            writeChunk (sdta, "sm24", sm24.getMemoryBlock());

        juce::MemoryOutputStream pdta;
        writeChunk (pdta, "phdr", phdr.getMemoryBlock());
        writeChunk (pdta, "pbag", pbag.getMemoryBlock());
        writeChunk (pdta, "pmod", pmod.getMemoryBlock());
        writeChunk (pdta, "pgen", pgen.getMemoryBlock());
        writeChunk (pdta, "inst", instChunk.getMemoryBlock());
        writeChunk (pdta, "ibag", ibag.getMemoryBlock());
        writeChunk (pdta, "imod", imod.getMemoryBlock());
        writeChunk (pdta, "igen", igen.getMemoryBlock());
        writeChunk (pdta, "shdr", shdr.getMemoryBlock());

        juce::MemoryOutputStream body;
        body.write ("sfbk", 4);
        writeChunk (body, "LIST", list ("INFO", info.getMemoryBlock()));
        writeChunk (body, "LIST", list ("sdta", sdta.getMemoryBlock()));
        writeChunk (body, "LIST", list ("pdta", pdta.getMemoryBlock()));

        const auto file = inst.folder.getChildFile (ExportHelpers::safeName (inst.name) + ".sf2");
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return ExportResult::failure ("No se pudo escribir " + file.getFullPathName());

        out.write ("RIFF", 4);
        out.writeInt ((int) body.getDataSize());
        out.write (body.getData(), body.getDataSize());
        out.flush();

        return ExportResult::success (file, inst.oneShot ? "SF2 no tiene modo one-shot: la nota suena mientras se mantiene" : juce::String());
    }
}

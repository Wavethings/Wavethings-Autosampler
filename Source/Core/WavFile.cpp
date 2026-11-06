#include "WavFile.h"

namespace wt
{
    namespace
    {
        void writeTag (juce::OutputStream& out, const char* tag) { out.write (tag, 4); }

        void writeSampleChunk (juce::OutputStream& out, const WavFile::WriteOptions& o)
        {
            const bool hasLoop = o.loop.enabled;
            writeTag (out, "smpl");
            out.writeInt (36 + (hasLoop ? 24 : 0));
            out.writeInt (0);                                           // manufacturer
            out.writeInt (0);                                           // product
            out.writeInt ((int) std::round (1.0e9 / o.sampleRate));     // sample period (ns)
            out.writeInt (juce::jlimit (0, 127, o.rootNote));           // MIDI unity note
            out.writeInt (0);                                           // pitch fraction
            out.writeInt (0);                                           // SMPTE format
            out.writeInt (0);                                           // SMPTE offset
            out.writeInt (hasLoop ? 1 : 0);                             // number of loops
            out.writeInt (0);                                           // sampler data

            if (hasLoop)
            {
                out.writeInt (0);                                       // cue point id
                out.writeInt (0);                                       // type: forward
                out.writeInt ((int) o.loop.start);
                out.writeInt ((int) o.loop.end);                        // inclusive
                out.writeInt (0);                                       // fraction
                out.writeInt (0);                                       // play count: infinite
            }
        }
    }

    bool WavFile::write (const juce::File& file, const juce::AudioBuffer<float>& audio, const WriteOptions& o)
    {
        file.deleteFile();
        file.getParentDirectory().createDirectory();

        juce::FileOutputStream out (file);
        if (! out.openedOk())
            return false;

        const int numChannels = audio.getNumChannels();
        const int numFrames = audio.getNumSamples();
        const bool isFloat = o.bitsPerSample == 32;
        const int bytesPerSample = o.bitsPerSample / 8;
        const juce::int64 dataBytes = (juce::int64) numFrames * numChannels * bytesPerSample;
        const bool writeSmpl = o.rootNote >= 0;
        const int smplBytes = writeSmpl ? 8 + 36 + (o.loop.enabled ? 24 : 0) : 0;
        const int fmtBytes = 8 + 16;
        const juce::int64 riffSize = 4 + fmtBytes + smplBytes + 8 + dataBytes + (dataBytes & 1);

        writeTag (out, "RIFF");
        out.writeInt ((int) riffSize);
        writeTag (out, "WAVE");

        writeTag (out, "fmt ");
        out.writeInt (16);
        out.writeShort ((short) (isFloat ? 3 : 1));
        out.writeShort ((short) numChannels);
        out.writeInt ((int) o.sampleRate);
        out.writeInt ((int) o.sampleRate * numChannels * bytesPerSample);
        out.writeShort ((short) (numChannels * bytesPerSample));
        out.writeShort ((short) o.bitsPerSample);

        if (writeSmpl)
            writeSampleChunk (out, o);

        writeTag (out, "data");
        out.writeInt ((int) dataBytes);

        juce::HeapBlock<char> block ((size_t) numChannels * (size_t) bytesPerSample * 4096);
        for (int pos = 0; pos < numFrames; pos += 4096)
        {
            const int n = juce::jmin (4096, numFrames - pos);
            auto* dst = block.get();

            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float v = audio.getSample (ch, pos + i);

                    if (isFloat)
                    {
                        float f = v;   // all supported targets are little-endian
                        std::memcpy (dst, &f, 4);
                        dst += 4;
                    }
                    else if (o.bitsPerSample == 16)
                    {
                        const int s = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 32767.0f);
                        dst[0] = (char) (s & 0xff);
                        dst[1] = (char) ((s >> 8) & 0xff);
                        dst += 2;
                    }
                    else
                    {
                        const int s = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 8388607.0f);
                        dst[0] = (char) (s & 0xff);
                        dst[1] = (char) ((s >> 8) & 0xff);
                        dst[2] = (char) ((s >> 16) & 0xff);
                        dst += 3;
                    }
                }
            }

            out.write (block.get(), (size_t) (dst - block.get()));
        }

        if (dataBytes & 1)
            out.writeByte (0);

        out.flush();
        return ! out.getStatus().failed();
    }

    bool WavFile::readInfo (const juce::File& file, Info& info)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return false;

        char tag[5] = {};
        if (in.read (tag, 4) != 4 || juce::String (tag) != "RIFF")
            return false;
        in.readInt();
        if (in.read (tag, 4) != 4 || juce::String (tag) != "WAVE")
            return false;

        bool haveFormat = false;

        while (! in.isExhausted())
        {
            if (in.read (tag, 4) != 4)
                break;

            const juce::int64 size = (juce::uint32) in.readInt();
            const juce::int64 chunkStart = in.getPosition();
            const juce::String id (tag);

            if (id == "fmt ")
            {
                const int format = (juce::uint16) in.readShort();
                info.numChannels = in.readShort();
                info.sampleRate = (double) in.readInt();
                in.readInt();
                in.readShort();
                info.bitsPerSample = in.readShort();
                info.isFloat = format == 3;
                if (format == 0xFFFE && size >= 40)
                {
                    in.setPosition (chunkStart + 24);
                    info.isFloat = in.readShort() == 3;
                }
                haveFormat = true;
            }
            else if (id == "data")
            {
                info.dataOffset = chunkStart;
                info.dataSize = size;
                if (haveFormat && info.numChannels > 0 && info.bitsPerSample > 0)
                    info.numFrames = size / (info.numChannels * (info.bitsPerSample / 8));
                return haveFormat;
            }

            in.setPosition (chunkStart + size + (size & 1));
        }

        return false;
    }

    bool WavFile::read (const juce::File& file, juce::AudioBuffer<float>& audio, Info& info)
    {
        if (! readInfo (file, info))
            return false;

        juce::FileInputStream in (file);
        if (! in.openedOk() || ! in.setPosition (info.dataOffset))
            return false;

        const int bytesPerSample = info.bitsPerSample / 8;
        if (bytesPerSample < 1 || bytesPerSample > 4)
            return false;

        audio.setSize (info.numChannels, (int) info.numFrames);
        juce::MemoryBlock raw;
        in.readIntoMemoryBlock (raw, (juce::pointer_sized_int) info.dataSize);
        auto* src = static_cast<const juce::uint8*> (raw.getData());

        for (int i = 0; i < (int) info.numFrames; ++i)
        {
            for (int ch = 0; ch < info.numChannels; ++ch)
            {
                float v = 0.0f;

                if (info.isFloat && bytesPerSample == 4)
                    std::memcpy (&v, src, 4);
                else if (bytesPerSample == 2)
                    v = (float) (juce::int16) (src[0] | (src[1] << 8)) / 32768.0f;
                else if (bytesPerSample == 3)
                {
                    int s = src[0] | (src[1] << 8) | (src[2] << 16);
                    if (s & 0x800000) s |= ~0xffffff;
                    v = (float) s / 8388608.0f;
                }
                else if (bytesPerSample == 4)
                {
                    const juce::int32 s = (juce::int32) (src[0] | (src[1] << 8) | (src[2] << 16) | ((juce::uint32) src[3] << 24));
                    v = (float) ((double) s / 2147483648.0);
                }
                else
                    v = ((float) src[0] - 128.0f) / 128.0f;

                audio.setSample (ch, i, v);
                src += bytesPerSample;
            }
        }

        return true;
    }
}

#include "SampleProcessing.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace wt::SampleProcessing
{
    float getPeak (const juce::AudioBuffer<float>& buffer, int start, int num)
    {
        if (num < 0)
            num = buffer.getNumSamples() - start;

        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, start, num));
        return peak;
    }

    std::vector<float> makeMono (const juce::AudioBuffer<float>& buffer, int numFrames)
    {
        if (numFrames < 0)
            numFrames = buffer.getNumSamples();

        std::vector<float> mono ((size_t) numFrames, 0.0f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numFrames; ++i)
                mono[(size_t) i] += src[i];
        }
        return mono;
    }

    int findRisingZeroCrossing (const float* mono, int numFrames, int from, int to)
    {
        from = juce::jlimit (1, numFrames - 1, from);
        to = juce::jlimit (1, numFrames - 1, to);
        const int dir = to >= from ? 1 : -1;

        for (int i = from; ; i += dir)
        {
            if (mono[i - 1] < 0.0f && mono[i] >= 0.0f)
                return i;
            if (i == to)
                break;
        }
        return -1;
    }

    static int findAnyZeroCrossing (const float* mono, int numFrames, int from, int to)
    {
        from = juce::jlimit (1, numFrames - 1, from);
        to = juce::jlimit (1, numFrames - 1, to);
        const int dir = to >= from ? 1 : -1;

        for (int i = from; ; i += dir)
        {
            if ((mono[i - 1] < 0.0f) != (mono[i] < 0.0f) || std::abs (mono[i]) < 1.0e-9f)
                return i;
            if (i == to)
                break;
        }
        return -1;
    }

    //==============================================================================
    bool findLoopPoints (const juce::AudioBuffer<float>& audio, int regionStart, int regionEnd, int minLoopLength,
                         int& loopStart, int& loopEndInclusive)
    {
        // The loop start is searched AROUND regionStart and the loop end AROUND regionEnd, so the loop
        // always spans (almost) the whole chosen region. Searching freely made the best match a tiny
        // loop at the very end of the sample, which sounds like a buzz with audible jumps.
        const int numFrames = audio.getNumSamples();
        regionStart = juce::jlimit (1, numFrames - 2, regionStart);
        regionEnd = juce::jlimit (regionStart + 1, numFrames - 2, regionEnd);

        const int regionLength = regionEnd - regionStart;
        if (regionLength < minLoopLength)
            return false;

        const auto mono = makeMono (audio);
        const float* m = mono.data();

        // search windows: 15 % of the region on each side (never more than a third of it)
        const int window = juce::jlimit (64, regionLength / 3, (int) (regionLength * 0.15));

        auto collectCrossings = [&] (int from, int to, size_t maxCount)
        {
            std::vector<int> found;
            from = juce::jlimit (1, numFrames - 1, from);
            to = juce::jlimit (1, numFrames - 1, to);
            for (int i = from; i <= to; ++i)
                if (m[i - 1] < 0.0f && m[i] >= 0.0f)
                    found.push_back (i);

            if (found.size() > maxCount)
            {
                std::vector<int> thinned;
                const double stride = (double) found.size() / (double) maxCount;
                for (size_t k = 0; k < maxCount; ++k)
                    thinned.push_back (found[(size_t) (k * stride)]);
                found.swap (thinned);
            }
            return found;
        };

        const auto startCandidates = collectCrossings (regionStart - window, regionStart + window, 400);
        const auto endCandidates = collectCrossings (regionEnd - window, juce::jmin (regionEnd + window / 4, numFrames - 2), 400);

        if (startCandidates.empty() || endCandidates.empty())
            return false;

        // Compare a few periods around the junction (all channels), the closest samples weigh more
        const int halfWindow = juce::jlimit (128, 2048, regionLength / 64);
        double bestScore = std::numeric_limits<double>::max();
        int bestStart = -1, bestEnd = -1;

        for (int endCrossing : endCandidates)
        {
            for (int startCrossing : startCandidates)
            {
                if (endCrossing - startCrossing < juce::jmax (minLoopLength, regionLength / 2))
                    continue;

                const int k0 = -juce::jmin (halfWindow, startCrossing);
                const int k1 = juce::jmin (halfWindow, numFrames - 1 - endCrossing);

                double score = 0.0, energy = 1.0e-12;
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                {
                    auto* d = audio.getReadPointer (ch);
                    for (int k = k0; k < k1; k += 2)
                    {
                        const double a = d[startCrossing + k];
                        const double b = d[endCrossing + k];
                        const double w = 1.0 - std::abs ((double) k) / (double) (halfWindow + 1);
                        score += w * (a - b) * (a - b);
                        energy += w * (a * a + b * b);
                    }
                }

                score /= energy;
                // small preference for the points the user asked for
                score *= 1.0 + 0.15 * (std::abs (startCrossing - regionStart) + std::abs (endCrossing - regionEnd)) / (double) window;

                if (score < bestScore)
                {
                    bestScore = score;
                    bestStart = startCrossing;
                    bestEnd = endCrossing;
                }
            }
        }

        if (bestStart < 0)
            return false;

        loopStart = bestStart;
        loopEndInclusive = bestEnd - 1;   // the sample after the loop end equals the crossing at the loop start
        return true;
    }

    void equaliseLoopLevel (juce::AudioBuffer<float>& audio, int loopStart, int loopEndInclusive, double sampleRate)
    {
        // Sounds that keep decaying while the note is held make a level jump at the loop point.
        // A smooth gain curve keeps the loop at the level of its start (max. +12 dB).
        const int length = loopEndInclusive - loopStart + 1;
        const int hop = juce::jmax (64, (int) (sampleRate * 0.01));
        const int win = hop * 4;
        if (length < win * 4)
            return;

        auto rmsAt = [&] (int centre)
        {
            const int s0 = juce::jlimit (0, audio.getNumSamples() - 1, centre - win / 2);
            const int n = juce::jmin (win, audio.getNumSamples() - s0);
            double e = 0.0;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                auto* d = audio.getReadPointer (ch, s0);
                for (int i = 0; i < n; ++i) e += (double) d[i] * d[i];
            }
            return std::sqrt (e / juce::jmax (1, n * audio.getNumChannels())) + 1.0e-9;
        };

        std::vector<double> env;
        for (int pos = loopStart; pos <= loopEndInclusive + hop; pos += hop)
            env.push_back (rmsAt (pos));

        // smooth the envelope so that only the slow decay is corrected
        std::vector<double> smooth (env.size());
        const int radius = 5;
        for (size_t i = 0; i < env.size(); ++i)
        {
            double sum = 0.0; int n = 0;
            for (int k = -radius; k <= radius; ++k)
            {
                const int j = (int) i + k;
                if (j >= 0 && j < (int) env.size()) { sum += env[(size_t) j]; ++n; }
            }
            smooth[i] = sum / n;
        }

        const double reference = smooth.front();
        const double maxGain = juce::Decibels::decibelsToGain (12.0);
        const double ratio = smooth.back() / reference;
        if (ratio > 0.94 && ratio < 1.06)
            return;   // already stable (< 0.5 dB), nothing to do

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* d = audio.getWritePointer (ch);
            for (int i = 0; i < length; ++i)
            {
                const double pos = (double) i / hop;
                const size_t i0 = juce::jmin ((size_t) pos, smooth.size() - 1);
                const size_t i1 = juce::jmin (i0 + 1, smooth.size() - 1);
                const double frac = pos - (double) i0;
                const double level = smooth[i0] * (1.0 - frac) + smooth[i1] * frac;
                d[loopStart + i] *= (float) juce::jlimit (1.0 / maxGain, maxGain, reference / level);
            }
        }
    }

    /** Correlation (-1..1) between two stretches of audio of the same length. */
    static double correlation (const juce::AudioBuffer<float>& a, int startA, const juce::AudioBuffer<float>& b, int startB, int length)
    {
        double ab = 0.0, aa = 1.0e-12, bb = 1.0e-12;
        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
        {
            auto* da = a.getReadPointer (ch, startA);
            auto* db = b.getReadPointer (ch, startB);
            for (int i = 0; i < length; ++i)
            {
                ab += (double) da[i] * db[i];
                aa += (double) da[i] * da[i];
                bb += (double) db[i] * db[i];
            }
        }
        return ab / std::sqrt (aa * bb);
    }

    /** Fade gains that keep the level constant: linear for similar (correlated) audio,
        equal power for unrelated audio, and a blend in between. */
    static void crossfadeGains (double t, double corr, float& fadeOut, float& fadeIn)
    {
        const double w = juce::jlimit (0.0, 1.0, corr);
        const double linOut = 1.0 - t, linIn = t;
        const double powOut = std::cos (t * juce::MathConstants<double>::halfPi);
        const double powIn = std::sin (t * juce::MathConstants<double>::halfPi);
        fadeOut = (float) (w * linOut + (1.0 - w) * powOut);
        fadeIn = (float) (w * linIn + (1.0 - w) * powIn);
    }

    void applyLoopCrossfade (juce::AudioBuffer<float>& audio, int loopStart, int loopEndInclusive, int crossfadeFrames)
    {
        const int loopLength = loopEndInclusive - loopStart + 1;
        const int x = juce::jmin (crossfadeFrames, loopStart, loopLength / 2);
        if (x <= 1)
            return;

        // the end of the loop is blended with the audio that precedes the loop start
        const double corr = correlation (audio, loopEndInclusive - x + 1, audio, loopStart - x, x);

        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto* d = audio.getWritePointer (ch);
            for (int k = 0; k < x; ++k)
            {
                float fadeOut, fadeIn;
                crossfadeGains (((double) k + 0.5) / (double) x, corr, fadeOut, fadeIn);
                const int dst = loopEndInclusive - x + 1 + k;
                const int src = loopStart - x + k;
                d[dst] = d[dst] * fadeOut + d[src] * fadeIn;
            }
        }
    }

    //==============================================================================
    static float cubicAt (const float* d, int length, double pos, bool wrap)
    {
        const int i = (int) std::floor (pos);
        const float t = (float) (pos - i);
        auto at = [&] (int k)
        {
            if (wrap) { k %= length; if (k < 0) k += length; }
            else k = juce::jlimit (0, length - 1, k);
            return d[k];
        };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        // Catmull-Rom
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }

    bool createPenroseLoop (const juce::AudioBuffer<float>& source, int regionStart, int regionEnd, int crossfadeFrames,
                            juce::uint32 randomSeed, juce::AudioBuffer<float>& result, LoopInfo& loop, double periodFrames)
    {
        const int regionLength = regionEnd - regionStart;
        if (regionLength < 4096)
            return false;

        const int numChannels = source.getNumChannels();
        const int maxOrder = 18;

        // Snapshot length: a whole number of periods of the fundamental, so that after resampling to a
        // power of two every harmonic falls exactly on an FFT bin (no beating between neighbouring bins).
        // Without a known pitch a plain power-of-two snapshot with a Hann window is used.
        const bool pitchSynchronous = periodFrames > 2.0 && regionLength / periodFrames >= 8.0;
        double snapshotLength;
        if (pitchSynchronous)
        {
            int periods = (int) std::floor (juce::jmin ((double) regionLength, (double) (1 << maxOrder)) / periodFrames);
            snapshotLength = periods * periodFrames;
        }
        else
        {
            int o = 12;
            while (o < maxOrder && (1 << (o + 1)) <= regionLength) ++o;
            snapshotLength = (double) (1 << o);
        }

        int order = 12;
        while (order < maxOrder && (double) (1 << order) < snapshotLength) ++order;
        const int n = 1 << order;
        const int snapshotStart = regionStart + (int) ((regionLength - snapshotLength) / 2.0);
        const double step = snapshotLength / (double) n;

        juce::dsp::FFT fft (order);
        std::vector<float> work ((size_t) n * 2);
        std::vector<std::vector<std::complex<float>>> spectra ((size_t) numChannels);

        // Analysis ---------------------------------------------------------------------
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            const float* src = source.getReadPointer (ch);

            for (int i = 0; i < n; ++i)
            {
                const float v = cubicAt (src, source.getNumSamples(), snapshotStart + i * step, false);
                const float w = pitchSynchronous ? 1.0f : 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) n);
                work[(size_t) i] = v * w;
            }

            fft.performRealOnlyForwardTransform (work.data(), true);

            auto& spectrum = spectra[(size_t) ch];
            spectrum.resize ((size_t) n / 2 + 1);
            for (int k = 0; k <= n / 2; ++k)
                spectrum[(size_t) k] = { work[(size_t) k * 2], work[(size_t) k * 2 + 1] };
        }

        // Resynthesis with new random phases (the phase difference between channels is kept) ---------
        juce::Random random ((juce::int64) randomSeed);
        std::vector<float> phases ((size_t) n / 2 + 1);
        for (auto& p : phases)
            p = random.nextFloat() * juce::MathConstants<float>::twoPi;

        const int loopLength = juce::jmax (64, juce::roundToInt (snapshotLength));
        juce::AudioBuffer<float> synth (numChannels, loopLength);
        std::vector<float> block ((size_t) n);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::fill (work.begin(), work.end(), 0.0f);

            for (int k = 1; k < n / 2; ++k)
            {
                const auto& ref = spectra[0][(size_t) k];
                const auto& bin = spectra[(size_t) ch][(size_t) k];
                const float stereoOffset = ch == 0 ? 0.0f : std::arg (bin) - std::arg (ref);
                const auto value = std::polar (std::abs (bin), phases[(size_t) k] + stereoOffset);

                work[(size_t) k * 2] = value.real();
                work[(size_t) k * 2 + 1] = value.imag();
                work[(size_t) (n - k) * 2] = value.real();
                work[(size_t) (n - k) * 2 + 1] = -value.imag();
            }

            fft.performRealOnlyInverseTransform (work.data());
            std::copy (work.begin(), work.begin() + n, block.begin());

            // back to the original time scale; the block is periodic, so interpolate circularly
            auto* dst = synth.getWritePointer (ch);
            for (int j = 0; j < loopLength; ++j)
                dst[j] = cubicAt (block.data(), n, (double) j * (double) n / (double) loopLength, true);
        }

        // Match the loudness of the snapshot -------------------------------------------
        const int snapLen = juce::jmin (loopLength, source.getNumSamples() - snapshotStart);
        double sourceEnergy = 0.0, synthEnergy = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* sp = source.getReadPointer (ch, snapshotStart);
            auto* y = synth.getReadPointer (ch);
            for (int i = 0; i < snapLen; ++i) sourceEnergy += (double) sp[i] * sp[i];
            for (int i = 0; i < loopLength; ++i) synthEnergy += (double) y[i] * y[i];
        }
        sourceEnergy /= juce::jmax (1, snapLen);
        synthEnergy /= loopLength;

        if (synthEnergy <= 1.0e-20)
            return false;

        synth.applyGain ((float) std::sqrt (sourceEnergy / synthEnergy));

        // Insert at a rising zero crossing near the start of the snapshot ---------------------
        const auto mono = makeMono (source);
        int insertAt = findRisingZeroCrossing (mono.data(), (int) mono.size(), snapshotStart, juce::jmax (1, snapshotStart - 2048));
        if (insertAt < 0)
            insertAt = snapshotStart;

        // start the synthetic block where its own waveform rises through zero as well
        const auto synthMono = makeMono (synth);
        int offset = findRisingZeroCrossing (synthMono.data(), loopLength, 1, loopLength - 1);
        if (offset < 0) offset = 0;

        const int x = juce::jlimit (0, juce::jmin (insertAt, loopLength / 2), crossfadeFrames);

        result.setSize (numChannels, insertAt + loopLength);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            result.copyFrom (ch, 0, source, ch, 0, insertAt);
            auto* d = result.getWritePointer (ch);
            auto* y = synth.getReadPointer (ch);

            for (int i = 0; i < loopLength; ++i)
                d[insertAt + i] = y[(i + offset) % loopLength];

            for (int k = 0; k < x; ++k)
            {
                // the resynthesis has new phases, so it is unrelated to the original: equal power
                float fadeOut, fadeIn;
                crossfadeGains (((double) k + 0.5) / (double) x, 0.0, fadeOut, fadeIn);
                const int dst = insertAt - x + k;
                d[dst] = d[dst] * fadeOut + y[(offset + loopLength - x + k) % loopLength] * fadeIn;   // periodic block
            }
        }

        loop.enabled = true;
        loop.start = insertAt;
        loop.end = insertAt + loopLength - 1;
        return true;
    }

    //==============================================================================
    static void copyRange (const juce::AudioBuffer<float>& src, int start, int num, juce::AudioBuffer<float>& dst)
    {
        dst.setSize (src.getNumChannels(), juce::jmax (0, num));
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            dst.copyFrom (ch, 0, src, ch, start, num);
    }

    static void applyFadeOut (juce::AudioBuffer<float>& audio, int fadeFrames)
    {
        const int total = audio.getNumSamples();
        fadeFrames = juce::jmin (fadeFrames, total);
        if (fadeFrames > 1)
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                audio.applyGainRamp (ch, total - fadeFrames, fadeFrames, 1.0f, 0.0f);
    }

    /** Appends a copy of the first frames of the loop after the loop end. Samplers that interpolate
        (or SF2 players, which require 8 valid points after the loop) then read the right audio. */
    static void appendLoopGuard (juce::AudioBuffer<float>& audio, const LoopInfo& loop)
    {
        const int guard = (int) std::min<juce::int64> (64, loop.getLength());
        const int end = (int) loop.end + 1;
        juce::AudioBuffer<float> extended (audio.getNumChannels(), end + guard);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            extended.copyFrom (ch, 0, audio, ch, 0, juce::jmin (end, audio.getNumSamples()));
            extended.copyFrom (ch, end, extended, ch, (int) loop.start, guard);
        }
        audio = std::move (extended);
    }

    ProcessedSample process (const juce::AudioBuffer<float>& raw, int numRecordedFrames, int noteOffFrame,
                             int latencyFrames, double sampleRate, const SamplingSettings& settings,
                             juce::uint32 randomSeed, int rootNote)
    {
        ProcessedSample result;
        numRecordedFrames = juce::jmin (numRecordedFrames, raw.getNumSamples());

        const int numChannels = settings.stereo ? juce::jmin (2, raw.getNumChannels()) : 1;
        juce::AudioBuffer<float> audio (numChannels, juce::jmax (1, numRecordedFrames));
        audio.clear();

        if (settings.stereo || raw.getNumChannels() == 1)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                audio.copyFrom (ch, 0, raw, ch, 0, numRecordedFrames);
        }
        else
        {
            audio.addFrom (0, 0, raw, 0, 0, numRecordedFrames, 0.5f);
            audio.addFrom (0, 0, raw, 1, 0, numRecordedFrames, 0.5f);
        }

        const float peak = getPeak (audio, 0, numRecordedFrames);
        result.peak = peak;
        const float threshold = juce::Decibels::decibelsToGain ((float) settings.thresholdDb);

        if (peak <= threshold || numRecordedFrames < 64)
        {
            result.isSilent = true;
            result.audio = std::move (audio);
            result.warnings.add ("El plugin no ha producido sonido por encima del umbral");
            return result;
        }

        const auto mono = makeMono (audio, numRecordedFrames);
        const float* m = mono.data();

        // 1) Start: skip the latency and the leading silence, cut on a zero crossing --------------
        int start = juce::jlimit (0, numRecordedFrames - 1, latencyFrames);
        if (settings.trimLeadingSilence)
        {
            const float onsetLevel = juce::jmin (threshold, peak * 0.01f);
            int onset = start;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                auto* d = audio.getReadPointer (ch);
                int i = start;
                while (i < numRecordedFrames && std::abs (d[i]) <= onsetLevel)
                    ++i;
                if (ch == 0 || i < onset)
                    onset = i;
            }

            const int maxBack = (int) (sampleRate * 0.005);
            const int zc = findAnyZeroCrossing (m, numRecordedFrames, onset, juce::jmax (start + 1, onset - maxBack));
            start = zc > 0 ? zc : juce::jmax (start, onset - 16);
        }

        // 2) End of the tail -----------------------------------------------------------
        int lastAbove = numRecordedFrames - 1;
        while (lastAbove > start && std::abs (m[lastAbove]) <= threshold * (float) audio.getNumChannels())
            --lastAbove;

        int end = findAnyZeroCrossing (m, numRecordedFrames, lastAbove, juce::jmin (numRecordedFrames - 1, lastAbove + (int) (sampleRate * 0.01)));
        if (end < 0)
            end = juce::jmin (numRecordedFrames - 1, lastAbove + 1);

        const int heldFrames = juce::jmax (1, noteOffFrame - start);
        result.heldFrames = heldFrames;
        result.releaseSeconds = juce::jmax (0.0, (double) (lastAbove - noteOffFrame) / sampleRate);

        juce::AudioBuffer<float> trimmed;
        copyRange (audio, start, juce::jmax (1, end - start + 1), trimmed);

        const int fadeFrames = (int) (sampleRate * settings.fadeOutMs / 1000.0);

        // 3) One-shot / no loop ----------------------------------------------------------
        if (! settings.isLooping())
        {
            applyFadeOut (trimmed, fadeFrames);
            result.audio = std::move (trimmed);
            return result;
        }

        // 4) Loops ---------------------------------------------------------------------
        const int held = juce::jmin (heldFrames, trimmed.getNumSamples() - 2);
        const double startPct = juce::jlimit (0.0, 99.0, settings.loopStartPercent) / 100.0;
        const double endPct = juce::jlimit (startPct * 100.0 + 1.0, 100.0, settings.loopEndPercent) / 100.0;
        const int regionStart = juce::jmax (1, (int) (held * startPct));
        const int regionEnd = juce::jmax (regionStart + 1, (int) (held * endPct));
        const int minLoop = juce::jmax (64, (int) (sampleRate * 0.1));
        const int crossfadeFrames = (int) (sampleRate * settings.crossfadeMs / 1000.0);

        if (settings.loopMode == LoopMode::penrose)
        {
            juce::AudioBuffer<float> looped;
            LoopInfo loop;
            const double period = rootNote >= 0 ? sampleRate / (440.0 * std::pow (2.0, (rootNote - 69) / 12.0)) : 0.0;
            if (createPenroseLoop (trimmed, regionStart, regionEnd, crossfadeFrames, randomSeed, looped, loop, period))
            {
                appendLoopGuard (looped, loop);
                result.audio = std::move (looped);
                result.loop = loop;
                return result;
            }

            result.warnings.add ("Región demasiado corta para Penrose: se usa loop con crossfade"_u8);
        }

        int loopStart = 0, loopEnd = 0;
        if (! findLoopPoints (trimmed, regionStart, regionEnd, minLoop, loopStart, loopEnd))
        {
            result.warnings.add ("No se encontraron puntos de loop (nota demasiado corta): se guarda sin loop");
            applyFadeOut (trimmed, fadeFrames);
            result.audio = std::move (trimmed);
            return result;
        }

        const int loopLength = loopEnd - loopStart + 1;
        juce::AudioBuffer<float> out;

        switch (settings.loopMode)
        {
            case LoopMode::backward:
            case LoopMode::backwardCrossfade:
            {
                // audio up to the loop end, followed by the loop region reversed; that block loops forward
                out.setSize (numChannels, loopEnd + 1 + loopLength);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    out.copyFrom (ch, 0, trimmed, ch, 0, loopEnd + 1);
                    auto* d = out.getWritePointer (ch, loopEnd + 1);
                    auto* s = trimmed.getReadPointer (ch);
                    for (int i = 0; i < loopLength; ++i)
                        d[i] = s[loopEnd - i];
                }
                result.loop = { true, loopEnd + 1, loopEnd + loopLength };
                break;
            }

            case LoopMode::pingPong:
            {
                // forward part + reversed part without repeating the turning points
                const int back = juce::jmax (0, loopLength - 2);
                out.setSize (numChannels, loopEnd + 1 + back);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    out.copyFrom (ch, 0, trimmed, ch, 0, loopEnd + 1);
                    auto* d = out.getWritePointer (ch, loopEnd + 1);
                    auto* s = trimmed.getReadPointer (ch);
                    for (int i = 0; i < back; ++i)
                        d[i] = s[loopEnd - 1 - i];
                }
                result.loop = { true, loopStart, loopEnd + back };
                break;
            }

            case LoopMode::forward:
            case LoopMode::forwardCrossfade:
            case LoopMode::penrose:      // fallback
            case LoopMode::off:
            default:
                copyRange (trimmed, 0, loopEnd + 1, out);
                result.loop = { true, loopStart, loopEnd };
                break;
        }

        if (settings.equaliseLoopLevel)
            equaliseLoopLevel (out, (int) result.loop.start, (int) result.loop.end, sampleRate);

        if (loopModeUsesCrossfade (settings.loopMode) && settings.renderCrossfade)
            applyLoopCrossfade (out, (int) result.loop.start, (int) result.loop.end, crossfadeFrames);
        else if (loopModeUsesCrossfade (settings.loopMode))
            result.loop.crossfadeFrames = juce::jmin (crossfadeFrames, (int) result.loop.start, (int) result.loop.getLength() / 2);

        appendLoopGuard (out, result.loop);
        result.audio = std::move (out);
        return result;
    }
}

#include "WaveformView.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/Text.h"

namespace wt
{
    void WaveformView::setSample (std::shared_ptr<ProcessedSample> s, const juce::String& l)
    {
        sample = std::move (s);
        label = l;
        computePeaks();
        repaint();
    }

    void WaveformView::clear()
    {
        sample.reset();
        peaks.clear();
        label.clear();
        repaint();
    }

    void WaveformView::computePeaks()
    {
        peaks.clear();
        if (sample == nullptr || getWidth() <= 0)
            return;

        const auto& audio = sample->audio;
        const int n = audio.getNumSamples();
        const int w = getWidth();
        peaks.resize ((size_t) w, { 0.0f, 0.0f });

        for (int x = 0; x < w; ++x)
        {
            const int s0 = (int) ((juce::int64) x * n / w);
            const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (x + 1) * n / w));
            float lo = 0.0f, hi = 0.0f;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            {
                auto r = audio.findMinMax (ch, s0, juce::jmin (s1, n) - s0);
                lo = juce::jmin (lo, r.getStart());
                hi = juce::jmax (hi, r.getEnd());
            }
            peaks[(size_t) x] = { lo, hi };
        }
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (b, 6.0f);

        if (sample == nullptr)
        {
            g.setColour (Colours::textDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("La última muestra grabada aparecerá aquí"_u8, getLocalBounds(), juce::Justification::centred);
            return;
        }

        const float mid = b.getCentreY();
        const float half = b.getHeight() * 0.45f;
        const int n = sample->audio.getNumSamples();

        if (sample->loop.enabled && n > 0)
        {
            const float x0 = (float) sample->loop.start / (float) n * b.getWidth();
            const float x1 = (float) (sample->loop.end + 1) / (float) n * b.getWidth();
            g.setColour (Colours::accent2.withAlpha (0.18f));
            g.fillRect (x0, b.getY(), x1 - x0, b.getHeight());
            g.setColour (Colours::accent2);
            g.drawVerticalLine ((int) x0, b.getY(), b.getBottom());
            g.drawVerticalLine ((int) x1, b.getY(), b.getBottom());
        }

        g.setColour (Colours::accent);
        for (size_t x = 0; x < peaks.size(); ++x)
            g.drawVerticalLine ((int) x, mid - peaks[x].second * half, mid - peaks[x].first * half + 1.0f);

        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        juce::String info = label;
        if (sample->loop.enabled)
            info << "   loop " << (juce::int64) sample->loop.start << " - " << (juce::int64) sample->loop.end;
        g.drawText (info, 8, 4, getWidth() - 16, 16, juce::Justification::centredLeft);
    }
}

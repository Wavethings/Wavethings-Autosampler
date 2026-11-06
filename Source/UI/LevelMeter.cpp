#include "LevelMeter.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/Text.h"

namespace wt
{
    LevelMeter::LevelMeter()
    {
        setRepaintsOnMouseActivity (false);
        startTimerHz (30);
    }

    void LevelMeter::setThresholdDb (double db)
    {
        thresholdDb = juce::jlimit (minDb, -6.0, db);
        repaint();
    }

    juce::Rectangle<float> LevelMeter::getMeterArea() const
    {
        return getLocalBounds().toFloat().withTrimmedBottom (42.0f).withTrimmedTop (6.0f).withSizeKeepingCentre (26.0f, (float) getHeight() - 48.0f)
                   .withX (34.0f);
    }

    float LevelMeter::dbToY (double db) const
    {
        const auto area = getMeterArea();
        const double norm = (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb);
        return area.getBottom() - (float) norm * area.getHeight();
    }

    double LevelMeter::yToDb (float y) const
    {
        const auto area = getMeterArea();
        const double norm = (area.getBottom() - y) / area.getHeight();
        return minDb + norm * (maxDb - minDb);
    }

    void LevelMeter::timerCallback()
    {
        const float level = getLevel != nullptr ? getLevel() : 0.0f;
        const float db = juce::Decibels::gainToDecibels (level, -120.0f);

        levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);   // smooth fall-off

        if (db >= peakHoldDb) { peakHoldDb = db; peakHoldCounter = 45; }
        else if (--peakHoldCounter <= 0) peakHoldDb = juce::jmax (-120.0f, peakHoldDb - 2.0f);

        belowThreshold = db < thresholdDb;
        repaint();
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        const auto area = getMeterArea();

        // scale
        g.setFont (juce::FontOptions (10.0f));
        for (int db : { 0, -12, -24, -36, -48, -60, -72, -84, -96 })
        {
            const float y = dbToY (db);
            g.setColour (Colours::textDim);
            g.drawText (juce::String (db), 0, (int) y - 6, 28, 12, juce::Justification::centredRight);
            g.setColour (Colours::outline);
            g.drawHorizontalLine ((int) y, area.getX() - 3.0f, area.getX());
        }

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (area, 3.0f);

        // level bar: green below the threshold, orange above it
        const float top = dbToY (levelDb);
        const float thresholdY = dbToY (thresholdDb);
        if (top < area.getBottom())
        {
            auto bar = area.withTop (top).reduced (3.0f, 0.0f);
            g.setColour (Colours::ok.withAlpha (0.9f));
            g.fillRect (bar.withTop (juce::jmax (bar.getY(), thresholdY)));
            if (top < thresholdY)
            {
                g.setColour (Colours::accent);
                g.fillRect (bar.withBottom (thresholdY));
            }
            if (levelDb > -1.0f)
            {
                g.setColour (Colours::danger);
                g.fillRect (bar.withBottom (dbToY (-1.0)));
            }
        }

        // peak hold
        g.setColour (Colours::text);
        g.drawHorizontalLine ((int) dbToY (peakHoldDb), area.getX() + 2.0f, area.getRight() - 2.0f);

        // threshold line + handle
        g.setColour (Colours::accent);
        g.fillRect (area.getX() - 4.0f, thresholdY - 1.0f, area.getWidth() + 8.0f, 2.0f);
        juce::Path handle;
        handle.addTriangle (area.getRight() + 4.0f, thresholdY, area.getRight() + 12.0f, thresholdY - 6.0f, area.getRight() + 12.0f, thresholdY + 6.0f);
        g.fillPath (handle);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (thresholdDb, 0) + " dB", (int) area.getRight() + 14, (int) thresholdY - 7, 60, 14, juce::Justification::centredLeft);

        // LED + text
        auto led = juce::Rectangle<float> (12.0f, 12.0f).withPosition (8.0f, (float) getHeight() - 34.0f);
        g.setColour (belowThreshold ? Colours::ok : Colours::accent);
        g.fillEllipse (led);
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (belowThreshold ? "Bajo el umbral: siguiente nota"_u8 : "Sonando: el sampler espera"_u8,
                          26, getHeight() - 40, getWidth() - 28, 24, juce::Justification::centredLeft, 2);
    }

    void LevelMeter::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

    void LevelMeter::mouseDrag (const juce::MouseEvent& e)
    {
        setThresholdDb (std::round (yToDb ((float) e.position.y)));
        if (onThresholdChanged != nullptr)
            onThresholdChanged (thresholdDb);
    }
}

#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "Core/PluginScanner.h"

namespace wt
{
    class WavethingsApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        // must be true: the plug-in scanner runs as a second instance of this executable
        bool moreThanOneInstanceAllowed() override { return true; }

        void initialise (const juce::String& commandLine) override
        {
            // Started as the plug-in scanning helper? Then do only that, without any window.
            scannerWorker = PluginScanner::startWorkerIfRequested (commandLine);
            if (scannerWorker != nullptr)
                return;

            juce::PropertiesFile::Options options;
            options.applicationName = "Wavethings Autosampler";
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName = ".config/Wavethings Autosampler";
           #else
            options.folderName = "Wavethings Autosampler";
           #endif
            properties.setStorageParameters (options);

            mainWindow = std::make_unique<MainWindow> (getApplicationName(), properties);
        }

        void shutdown() override
        {
            scannerWorker.reset();
            mainWindow.reset();
            properties.closeFiles();
        }

        void systemRequestedQuit() override { quit(); }

        /** Holds the main component and scales it (interface size 75-200 %). */
        class ScaledContent : public juce::Component
        {
        public:
            explicit ScaledContent (juce::ApplicationProperties& props) : main (props)
            {
                addAndMakeVisible (main);
                setSize ((int) (MainComponent::baseWidth * main.getUiScale()), (int) (MainComponent::baseHeight * main.getUiScale()));
            }

            void resized() override
            {
                const float s = main.getUiScale();
                main.setTransform (juce::AffineTransform::scale (s));
                main.setBounds (0, 0, juce::roundToInt ((float) getWidth() / s), juce::roundToInt ((float) getHeight() / s));
            }

            MainComponent main;
        };

        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow (const juce::String& name, juce::ApplicationProperties& props)
                : DocumentWindow (name, juce::Colour (0xff111317), DocumentWindow::allButtons)
            {
                setUsingNativeTitleBar (true);
                auto* content = new ScaledContent (props);
                setContentOwned (content, true);
                setResizable (true, true);
                content->main.onScaleChanged = [this, content] (float) { applyScale (*content, true); };
                applyScale (*content, false);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
                content->main.grabKeyboardFocus();
            }

            void applyScale (ScaledContent& content, bool keepLogicalSize)
            {
                const float s = content.main.getUiScale();
                auto logical = keepLogicalSize ? content.main.getBounds().getWidth() : MainComponent::baseWidth;
                auto logicalH = keepLogicalSize ? content.main.getBounds().getHeight() : MainComponent::baseHeight;

                // never larger than the screen
                auto area = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()) != nullptr
                                ? juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())->userArea
                                : juce::Rectangle<int> (0, 0, 1920, 1080);
                const int maxW = area.getWidth() - 20, maxH = area.getHeight() - 60;

                const int minW = juce::jmin (maxW, (int) (1180 * s)), minH = juce::jmin (maxH, (int) (860 * s));
                setResizeLimits (minW, minH, 8000, 6000);

                const int w = juce::jlimit (minW, juce::jmax (minW, maxW), (int) ((float) logical * s));
                const int h = juce::jlimit (minH, juce::jmax (minH, maxH), (int) ((float) logicalH * s));
                content.setSize (w, h);
                content.resized();
                if (keepLogicalSize && ! area.contains (getBounds()))
                    setTopLeftPosition (juce::jmax (area.getX(), juce::jmin (getX(), area.getRight() - getWidth())),
                                        juce::jmax (area.getY(), juce::jmin (getY(), area.getBottom() - getHeight())));
            }

            // keys that no child used (Cmd/Ctrl + / - / 0 for the interface size)
            bool keyPressed (const juce::KeyPress& key) override
            {
                if (auto* content = dynamic_cast<ScaledContent*> (getContentComponent()))
                    return content->main.keyPressed (key);
                return false;
            }

            void closeButtonPressed() override
            {
                JUCEApplication::getInstance()->systemRequestedQuit();
            }

        private:
            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
        };

    private:
        juce::ApplicationProperties properties;
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<juce::ChildProcessWorker> scannerWorker;
    };
}

START_JUCE_APPLICATION (wt::WavethingsApplication)

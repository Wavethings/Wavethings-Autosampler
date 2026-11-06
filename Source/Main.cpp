#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

namespace wt
{
    class WavethingsApplication : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise (const juce::String&) override
        {
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
            mainWindow.reset();
            properties.closeFiles();
        }

        void systemRequestedQuit() override { quit(); }

        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow (const juce::String& name, juce::ApplicationProperties& props)
                : DocumentWindow (name, juce::Colour (0xff111317), DocumentWindow::allButtons)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (new MainComponent (props), true);
                setResizable (true, true);
                setResizeLimits (1180, 860, 4000, 3000);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
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
    };
}

START_JUCE_APPLICATION (wt::WavethingsApplication)

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
        std::unique_ptr<juce::ChildProcessWorker> scannerWorker;
    };
}

START_JUCE_APPLICATION (wt::WavethingsApplication)

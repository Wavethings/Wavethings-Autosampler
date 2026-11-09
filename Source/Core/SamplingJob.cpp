#include "SamplingJob.h"
#include "Text.h"
#include "WavFile.h"
#include "../Export/Exporter.h"

namespace wt
{
    juce::String makeSafeFileName (const juce::String& name)
    {
        auto safe = juce::File::createLegalFileName (name.trim()).replaceCharacters ("/\\:*?\"<>|", "_________");
        safe = safe.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.#()+&,");
        return safe.isEmpty() ? juce::String ("Instrument") : safe.trim();
    }

    SamplingJob::SamplingJob (AudioEngine& e, const SamplingSettings& s)
        : juce::Thread ("Wavethings Sampling"), engine (e), settings (s)
    {
    }

    SamplingJob::~SamplingJob()
    {
        stopThread (10000);
    }

    void SamplingJob::post (std::function<void()> fn)
    {
        if (synchronousCallbacks)
            fn();
        else
            juce::MessageManager::callAsync (std::move (fn));
    }

    void SamplingJob::log (const juce::String& line)
    {
        if (onLog != nullptr)
        {
            auto cb = onLog;
            post ([cb, line] { cb (line); });
        }
    }

    void SamplingJob::sleepFor (double ms)
    {
        if (offlineRenderer != nullptr)
            offlineRenderer ((int) (engine.getSampleRate() * ms / 1000.0));
        else
            wait ((int) ms);
    }

    bool SamplingJob::waitForSilence (double maxSeconds)
    {
        const float threshold = juce::Decibels::decibelsToGain ((float) settings.thresholdDb);
        const double start = juce::Time::getMillisecondCounterHiRes();
        double quietSince = -1.0;
        double simulatedMs = 0.0;
        engine.getAndResetPeak();

        while (! threadShouldExit())
        {
            sleepFor (10.0);
            simulatedMs += 10.0;
            const double now = offlineRenderer != nullptr ? simulatedMs : juce::Time::getMillisecondCounterHiRes() - start;
            const float peak = engine.getAndResetPeak();

            if (peak < threshold)
            {
                if (quietSince < 0.0)
                    quietSince = now;
                if (now - quietSince >= settings.silenceHoldMs)
                    return true;
            }
            else
            {
                quietSince = -1.0;
            }

            if (now > maxSeconds * 1000.0)
                return false;
        }
        return false;
    }

    bool SamplingJob::captureNote (int note, int velocity, CaptureRequest& r)
    {
        const double sr = engine.getSampleRate();
        const int latency = engine.getRecordingLatency();

        r.note = note;
        r.midiChannel = settings.midiChannel;
        r.velocity = velocity;
        r.holdFrames = juce::jmax (1, (int) (settings.noteHoldSeconds * sr));
        r.maxFrames = r.holdFrames + (int) (settings.maxReleaseSeconds * sr) + latency;
        r.threshold = juce::Decibels::decibelsToGain ((float) settings.thresholdDb);
        r.silenceFrames = juce::jmax (1, (int) (settings.silenceHoldMs * sr / 1000.0));
        r.minTailFrames = latency + (int) (0.02 * sr);
        r.buffer.setSize (2, r.maxFrames);
        r.buffer.clear();

        engine.startCapture (&r);

        const double timeoutMs = (double) r.maxFrames / sr * 1000.0 + 5000.0;
        const double start = juce::Time::getMillisecondCounterHiRes();

        while (! r.finished.load())
        {
            if (threadShouldExit())
                r.abortRequested = true;

            if (offlineRenderer != nullptr)
                offlineRenderer (engine.getBlockSize());
            else
                wait (5);

            if (offlineRenderer == nullptr && juce::Time::getMillisecondCounterHiRes() - start > timeoutMs)
            {
                engine.stopCapture (&r);
                // give a running audio callback time to leave the request
                wait (200);
                log ("Tiempo de espera agotado: ¿está funcionando el dispositivo de audio?"_u8);
                return false;
            }
        }

        return ! r.abortRequested.load();
    }

    void SamplingJob::run()
    {
        const bool hardware = settings.source == SoundSource::hardware;

        // Silent mode / faster than real time -----------------------------------------------
        struct BackgroundModeGuard
        {
            SamplingJob& job;
            bool offline = false;
            ~BackgroundModeGuard()
            {
                job.engine.sendAllNotesOff();
                if (offline)
                {
                    job.engine.setOfflineMode (false);
                    job.offlineRenderer = nullptr;
                }
                job.engine.setMuteOutput (false);
            }
        } guard { *this };

        auto fail = [this] (const juce::String& message)
        {
            log (message);
            post ([this] { if (onFinished) onFinished (false, instrument); });
        };

        if (hardware)
        {
            if (engine.getMidiOutputIdentifier().isEmpty() && engine.hardwareSimulator == nullptr)
                return fail ("Elige la salida MIDI que va a tu instrumento (sección Instrumento, modo Hardware)");
            if (settings.silentMode)
                engine.setMuteOutput (true);
            log ("Instrumento hardware: notas por " + (engine.hardwareSimulator != nullptr ? juce::String ("(simulador)") : engine.getMidiOutputIdentifier())
                 + ", canal " + juce::String (settings.midiChannel) + ". Latencia compensada: "
                 + juce::String (engine.getRecordingLatency() * 1000.0 / engine.getSampleRate(), 1) + " ms");
        }
        else
        {
            if (engine.getPlugin() == nullptr)
                return fail ("No hay ningún plugin cargado"_u8);

            if (settings.silentMode)
            {
                engine.setMuteOutput (true);

                if (settings.fastRender && offlineRenderer == nullptr)
                {
                    engine.setOfflineMode (true);
                    guard.offline = true;
                    offlineBuffer.setSize (2, juce::jmax (64, engine.getBlockSize()));
                    offlineRenderer = [this] (int frames)
                    {
                        juce::MidiBuffer none;
                        for (int done = 0; done < frames; done += offlineBuffer.getNumSamples())
                            engine.processOffline (offlineBuffer, none);
                    };
                    log ("Modo silencio rápido: se renderiza más rápido que en tiempo real"_u8);
                }
                else
                {
                    log ("Modo silencio: se graba sin sonido en los altavoces");
                }
            }
        }
        engine.setMidiChannel (settings.midiChannel);

        if (settings.getRootNotes().empty())
            return fail ("No hay ninguna nota seleccionada para samplear");

        // Folders ------------------------------------------------------------------
        auto root = settings.outputFolder;
        if (root == juce::File())
            root = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Wavethings Autosampler");

        const auto items = settings.getBatchItems();
        const bool batch = settings.batchEnabled;
        if (batch)
        {
            // one folder for the batch, one sub-folder per program
            const auto safeName = makeSafeFileName (settings.instrumentName);
            auto batchFolder = root.getChildFile (safeName);
            for (int i = 2; batchFolder.exists(); ++i)
                batchFolder = root.getChildFile (safeName + " " + juce::String (i));
            root = batchFolder;
            outputFolder = batchFolder;
            log ("Lote: "_u8 + juce::String ((int) items.size()) + " programas en " + batchFolder.getFullPathName());
        }

        int finished = 0, skipped = 0;
        bool cancelled = false;
        Instrument lastGood;

        for (size_t i = 0; i < items.size() && ! cancelled; ++i)
        {
            if (threadShouldExit()) { cancelled = true; break; }

            const auto& item = items[i];
            juce::String name = settings.instrumentName;

            if (batch)
            {
                if (! selectProgram (item, name))
                {
                    ++skipped;
                    continue;
                }
            }

            programIndex = (int) i + 1;
            programTotal = (int) items.size();
            programName = batch ? name : juce::String();

            const auto result = sampleInstrument (root, name, batch);
            if (result == Result::ok)          { ++finished; lastGood = instrument; }
            else if (result == Result::empty)  { ++skipped; }
            else if (result == Result::cancelled) cancelled = true;
            else if (! batch)                  break;   // a single instrument that failed
        }

        engine.sendAllNotesOff();

        if (batch)
        {
            log ("Lote terminado: "_u8 + juce::String (finished) + " instrumentos"
                 + (skipped > 0 ? ", " + juce::String (skipped) + " programas omitidos" : juce::String())
                 + (cancelled ? " (cancelado)" : ""));
            if (finished > 0)
                instrument = lastGood;
            instrument.folder = root;
        }
        else if (instrument.folder != juce::File())
        {
            outputFolder = instrument.folder;
        }

        const bool ok = finished > 0 && ! cancelled;
        post ([this, ok] { if (onFinished) onFinished (ok, instrument); });
    }

    bool SamplingJob::selectProgram (const BatchItem& item, juce::String& name)
    {
        const bool pluginPrograms = settings.programMethod == ProgramChangeMethod::pluginPrograms && settings.source == SoundSource::plugin;
        const int channel = settings.midiChannel;
        juce::String label;

        engine.sendAllNotesOff();

        if (pluginPrograms)
        {
            auto* plugin = engine.getPlugin();
            if (plugin == nullptr || item.program >= plugin->getNumPrograms())
            {
                log ("El plugin no tiene el programa " + juce::String (item.program + 1) + ": se omite");
                return false;
            }
            engine.setPluginProgram (item.program);
            label = "P" + juce::String (item.program + 1).paddedLeft ('0', 3);
            if (settings.usePluginProgramNames)
            {
                const auto programName = plugin->getProgramName (item.program).trim();
                if (programName.isNotEmpty())
                    label = programName;
            }
            log ("Programa del plugin "_u8 + juce::String (item.program + 1) + ": " + label);
        }
        else
        {
            if (item.bankMsb >= 0) { engine.queueMidi (juce::MidiMessage::controllerEvent (channel, 0, item.bankMsb));  label << "B" << item.bankMsb; }
            if (item.bankLsb >= 0) { engine.queueMidi (juce::MidiMessage::controllerEvent (channel, 32, item.bankLsb)); label << (item.bankMsb >= 0 ? "." : "B") << item.bankLsb; }
            engine.queueMidi (juce::MidiMessage::programChange (channel, item.program));
            label << (label.isNotEmpty() ? " " : "") << "P" << juce::String (item.program + 1).paddedLeft ('0', 3);
            log ("Cambio de programa: "_u8 + label + " (canal " + juce::String (channel) + ")");
            label = settings.instrumentName + " " + label;
        }

        name = item.name.isNotEmpty() ? item.name : label;

        // let the instrument load the new sound
        sleepFor (juce::jmax (0.0, settings.programChangeWaitMs));
        return ! threadShouldExit();
    }

    SamplingJob::Result SamplingJob::sampleInstrument (const juce::File& root, const juce::String& name, bool batch)
    {
        const double sr = engine.getSampleRate();
        const bool hardware = settings.source == SoundSource::hardware;
        const int latency = engine.getRecordingLatency();

        const auto safeName = makeSafeFileName (name);
        auto folder = root.getChildFile (safeName);
        for (int i = 2; folder.exists(); ++i)
            folder = root.getChildFile (safeName + " " + juce::String (i));

        instrument = {};
        instrument.name = folder.getFileName();
        instrument.creator = settings.creator;
        instrument.category = settings.category;
        instrument.description = settings.description;
        instrument.folder = folder;
        instrument.sampleFolder = folder.getChildFile ("Samples");
        instrument.numVelocityLayers = settings.numVelocityLayers;
        instrument.oneShot = settings.oneShot;
        instrument.looped = settings.isLooping();
        instrument.sampleRate = sr;
        instrument.bitsPerSample = (int) settings.bitDepth;

        captureFolder = folder.getChildFile (".capture");
        if (! captureFolder.createDirectory() || ! instrument.sampleFolder.createDirectory())
        {
            log ("No se pudo crear la carpeta " + folder.getFullPathName());
            return Result::failed;
        }

        // Plan -----------------------------------------------------------------------
        const auto roots = settings.getRootNotes();
        const auto ranges = settings.getKeyRanges();
        const auto layers = computeVelocityLayers (settings.numVelocityLayers, settings.velocityPlayPoint);
        const int total = (int) (roots.size() * layers.size());

        log ("Sampleando \"" + instrument.name + "\": " + juce::String ((int) roots.size()) + " notas x "
             + juce::String ((int) layers.size()) + " capas = " + juce::String (total) + " muestras");

        engine.sendAllNotesOff();
        float globalPeak = 0.0f;
        std::vector<double> releaseTimes;
        int index = 0;
        bool cancelled = false;

        auto noteName = [this] (int note) { return NoteUtils::getNoteName (note, settings.octaveForMiddleC); };

        for (size_t r = 0; r < roots.size() && ! cancelled; ++r)
        {
            for (size_t l = 0; l < layers.size(); ++l)
            {
                if (threadShouldExit()) { cancelled = true; break; }

                const int note = roots[r];
                const auto& layer = layers[l];
                ++index;

                Progress p;
                p.index = index; p.total = total; p.note = note; p.velocity = layer.played; p.layer = (int) l;
                p.programIndex = programIndex; p.programTotal = programTotal; p.programName = programName;
                p.status = "Esperando silencio...";
                if (onProgress) { auto cb = onProgress; post ([cb, p] { cb (p); }); }

                // never start a note while the previous one still sounds
                if (! waitForSilence (settings.maxWaitForSilenceSeconds))
                {
                    engine.sendAllNotesOff();   // a hanging note? (also releases the sustain pedal)
                    if (! waitForSilence (settings.maxWaitForSilenceSeconds))
                        log ("Aviso: el sonido no baja del umbral antes de " + noteName (note)
                             + (hardware ? ". Mide el ruido de fondo (botón «Medir ruido») o sube el umbral."_u8
                                         : ". Sube el umbral o desactiva efectos (delay/reverb)."_u8));
                    else
                        log (noteName (note) + ": había una nota colgada, se envió All Notes Off");
                }

                sleepFor (settings.gapMs);

                p.status = "Grabando " + noteName (note) + " vel " + juce::String (layer.played);
                if (onProgress) { auto cb = onProgress; post ([cb, p] { cb (p); }); }

                engine.getAndResetClipped();
                CaptureRequest request;
                if (! captureNote (note, layer.played, request))
                {
                    cancelled = threadShouldExit();
                    if (cancelled) break;
                    continue;
                }

                if (hardware && engine.getAndResetClipped())
                    log (noteName (note) + " vel " + juce::String (layer.played)
                         + ": la entrada llega a 0 dBFS (satura). Baja el volumen del instrumento o la ganancia de la interfaz."_u8);

                const int recorded = request.recordedFrames.load();
                const int noteOff = juce::jmax (0, request.noteOffFrame.load());

                auto processed = std::make_shared<ProcessedSample> (
                    SampleProcessing::process (request.buffer, recorded, noteOff, latency, sr, settings,
                                               (juce::uint32) (note * 131 + layer.played), note));

                for (auto& w : processed->warnings)
                    log (noteName (note) + ": " + w);

                if (processed->isSilent)
                {
                    log ("Se omite " + noteName (note) + " vel " + juce::String (layer.played) + " (silencio)");
                    continue;
                }

                if (request.recordedFrames.load() >= request.maxFrames)
                    log (noteName (note) + ": la cola supera el tiempo máximo de release"_u8);

                globalPeak = juce::jmax (globalPeak, processed->peak);
                releaseTimes.push_back (processed->releaseSeconds);

                SampleZone zone;
                zone.fileName = makeSafeFileName (instrument.name) + "_" + NoteUtils::getFileSafeNoteName (note, settings.octaveForMiddleC)
                                + "_" + juce::String (layer.played).paddedLeft ('0', 3) + ".wav";
                zone.rootNote = note;
                zone.lowKey = ranges[r].low;
                zone.highKey = ranges[r].high;
                zone.lowVelocity = layer.low;
                zone.highVelocity = layer.high;
                zone.layerIndex = (int) l;
                zone.playedVelocity = layer.played;
                zone.sampleRate = sr;
                zone.numChannels = processed->audio.getNumChannels();
                zone.numFrames = processed->audio.getNumSamples();
                zone.loop = processed->loop;
                zone.oneShot = settings.oneShot;

                WavFile::WriteOptions o;
                o.bitsPerSample = 32;
                o.sampleRate = sr;
                o.rootNote = note;
                o.loop = zone.loop;
                if (! WavFile::write (captureFolder.getChildFile (zone.fileName), processed->audio, o))
                {
                    log ("Error al escribir " + zone.fileName);
                    continue;
                }

                instrument.zones.push_back (zone);

                if (onSampleCaptured) { auto cb = onSampleCaptured; post ([cb, processed, note, v = layer.played] { cb (processed, note, v); }); }
            }

            // a program that doesn't sound at all: don't waste time on the rest of its notes
            if (batch && settings.skipSilentPrograms && r == 0 && instrument.zones.empty() && ! cancelled)
            {
                log ("\"" + instrument.name + "\" no suena: se omite el programa");
                folder.deleteRecursively();
                return Result::empty;
            }
        }

        engine.sendAllNotesOff();

        if (cancelled)
        {
            log ("Sampleo cancelado. Las muestras grabadas se conservan en " + captureFolder.getFullPathName());
            return Result::cancelled;
        }

        if (instrument.zones.empty())
        {
            log ("No se ha grabado ninguna muestra de \"" + instrument.name + "\". Comprueba el umbral y que el instrumento suena.");
            folder.deleteRecursively();
            return batch ? Result::empty : Result::failed;
        }

        // Release time for the sampler's amplitude envelope: median of all tails
        std::sort (releaseTimes.begin(), releaseTimes.end());
        instrument.releaseSeconds = juce::jlimit (0.02, 10.0, releaseTimes[releaseTimes.size() / 2]);

        if (! finaliseSamples (globalPeak))
            return Result::failed;

        runExporters();
        log ("Terminado: " + instrument.folder.getFullPathName());
        return Result::ok;
    }

    bool SamplingJob::finaliseSamples (float globalPeak)
    {
        float gain = 1.0f;
        if (settings.normalize && globalPeak > 0.0f)
        {
            gain = juce::Decibels::decibelsToGain ((float) settings.normalizeTargetDb) / globalPeak;
            log ("Normalización: "_u8 + juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB");
        }
        else if (globalPeak > 1.0f && settings.bitDepth != SampleBitDepth::float32)
        {
            gain = 0.999f / globalPeak;
            log ("El plugin satura por encima de 0 dBFS: se reduce " + juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB");
        }

        for (auto& zone : instrument.zones)
        {
            juce::AudioBuffer<float> audio;
            WavFile::Info info;
            const auto src = captureFolder.getChildFile (zone.fileName);
            if (! WavFile::read (src, audio, info))
            {
                log ("Error al leer " + src.getFullPathName());
                return false;
            }

            audio.applyGain (gain);

            WavFile::WriteOptions o;
            o.bitsPerSample = (int) settings.bitDepth;
            o.sampleRate = zone.sampleRate;
            o.rootNote = zone.rootNote;
            o.loop = zone.loop;

            if (! WavFile::write (instrument.getSampleFile (zone), audio, o))
            {
                log ("Error al escribir " + zone.fileName);
                return false;
            }

            zone.bitsPerSample = o.bitsPerSample;
            zone.isFloat = o.bitsPerSample == 32;
        }

        captureFolder.deleteRecursively();
        return true;
    }

    void SamplingJob::runExporters()
    {
        ExportOptions options;
        options.convertWithMossExecutable = settings.convertWithMossExecutable;
        options.log = [this] (const juce::String& s) { log (s); };

        for (auto& id : settings.exportFormats)
        {
            auto exporter = createExporter (id);
            if (exporter == nullptr)
            {
                log ("Formato desconocido: " + id);
                continue;
            }

            const auto result = exporter->exportInstrument (instrument, options);
            log ((result.ok ? juce::String ("OK  ") : juce::String ("ERROR  ")) + exporter->getDisplayName() + ": " + result.message);
        }
    }
}

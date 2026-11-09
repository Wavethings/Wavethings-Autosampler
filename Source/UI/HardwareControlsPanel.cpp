#include "HardwareControlsPanel.h"
#include "WavethingsLookAndFeel.h"
#include "../Core/Text.h"

namespace wt
{
    namespace
    {
        struct CommonCc { const char* name; int cc; bool button; };

        // General MIDI / most common synth controllers
        const CommonCc commonCcs[] = {
            { "Cutoff", 74, false },        { "Resonancia", 71, false },  { "Ataque", 73, false },
            { "Release", 72, false },       { "Decay", 75, false },       { "Volumen", 7, false },
            { "Modulación", 1, false },     { "Expresión", 11, false },   { "Pan", 10, false },
            { "Portamento", 5, false },     { "Reverb", 91, false },      { "Chorus", 93, false },
            { "Vibrato", 76, false },       { "Sustain (pedal)", 64, true }, { "Portamento on/off", 65, true },
        };
    }

    //==============================================================================
    class HardwareControlsPanel::ControlView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ControlView (HardwareControlsPanel& p, int i) : panel (p), index (i)
        {
            const auto& c = panel.controls[(size_t) index];

            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 127.0, 1.0);
            slider.setPopupDisplayEnabled (true, true, nullptr);
            slider.setValue (c.value, juce::dontSendNotification);
            slider.setDoubleClickReturnValue (true, 64.0);
            slider.onValueChange = [this] { panel.setValue (index, (int) slider.getValue(), true); };
            addChildComponent (slider);
            slider.setVisible (! c.isButton);

            toggle.setClickingTogglesState (true);
            toggle.setToggleState (c.value >= 64, juce::dontSendNotification);
            toggle.onClick = [this] { panel.setValue (index, toggle.getToggleState() ? 127 : 0, true); };
            addChildComponent (toggle);
            toggle.setVisible (c.isButton);

            for (auto* l : { &name, &valueLabel, &ccLabel })
            {
                l->setJustificationType (juce::Justification::centred);
                l->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (*l);
            }
            name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            name.setMinimumHorizontalScale (0.6f);
            valueLabel.setFont (juce::FontOptions (11.0f));
            valueLabel.setColour (juce::Label::textColourId, Colours::textDim);
            ccLabel.setFont (juce::FontOptions (10.5f, juce::Font::bold));

            menuButton.setButtonText ("...");
            menuButton.setTooltip ("Nombre, CC, canal, perilla o botón, aprender, quitar"_u8);
            menuButton.onClick = [this] { panel.showControlMenu (index, menuButton); };
            addAndMakeVisible (menuButton);

            setTooltip ("Clic derecho o «...»: editar. Doble clic: aprender el CC de tu teclado MIDI"_u8);
            sync();
        }

        void sync()
        {
            const auto& c = panel.controls[(size_t) index];
            name.setText (c.name, juce::dontSendNotification);
            if (! slider.isMouseButtonDown())
                slider.setValue (c.value, juce::dontSendNotification);
            toggle.setToggleState (c.value >= 64, juce::dontSendNotification);
            toggle.setButtonText (c.value >= 64 ? "ON" : "OFF");
            valueLabel.setText (c.isButton ? juce::String() : juce::String (c.value), juce::dontSendNotification);

            const bool waiting = panel.learning && panel.learnTarget == index;
            const bool blink = waiting && (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
            ccLabel.setText (waiting ? "mueve un CC..." : "CC " + juce::String (c.cc) + (c.channel > 0 ? " · ch "_u8 + juce::String (c.channel) : juce::String()),
                             juce::dontSendNotification);
            ccLabel.setColour (juce::Label::textColourId, waiting ? (blink ? Colours::accent : Colours::text) : Colours::accent2);
            if (waiting != wasWaiting) { wasWaiting = waiting; repaint(); }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                panel.showControlMenu (index, *this);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            panel.learning = true;
            panel.learnTarget = index;
            panel.updateStatus();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Colours::panelLight);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            if (wasWaiting)
            {
                g.setColour (Colours::accent);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.5f);
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (4, 3);
            auto top = b.removeFromTop (14);
            menuButton.setBounds (top.removeFromRight (20));
            ccLabel.setBounds (top);
            name.setBounds (b.removeFromBottom (14));
            valueLabel.setBounds (b.removeFromBottom (13));
            slider.setBounds (b);
            toggle.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth() - 8, 70), juce::jmin (b.getHeight() - 4, 34)));
        }

        HardwareControlsPanel& panel;
        int index;
        juce::Slider slider;
        juce::TextButton toggle, menuButton;
        juce::Label name, valueLabel, ccLabel;
        bool wasWaiting = false;
    };

    //==============================================================================
    HardwareControlsPanel::HardwareControlsPanel()
    {
        addButton.setButtonText ("Añadir control"_u8);
        addButton.setTooltip ("Añade un control que envía un CC al instrumento (Cutoff, Resonancia, Ataque...)"_u8);
        addButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addButton);

        learnButton.setButtonText ("Aprender del teclado");
        learnButton.setClickingTogglesState (true);
        learnButton.setTooltip ("Mueve los mandos de tu controlador MIDI: cada CC nuevo se añade como un control. Pulsa otra vez para terminar"_u8);
        learnButton.onClick = [this]
        {
            learning = learnButton.getToggleState();
            learnTarget = -1;
            lastLearned = {};
            updateStatus();
        };
        addAndMakeVisible (learnButton);

        sendAllButton.setButtonText ("Enviar todo");
        sendAllButton.setTooltip ("Envía al instrumento el valor actual de todos los controles"_u8);
        sendAllButton.onClick = [this] { sendAll(); };
        addAndMakeVisible (sendAllButton);

        sendOnStartToggle.setButtonText ("Enviar al empezar y tras cada programa");
        sendOnStartToggle.setTooltip ("Antes de samplear (y después de cada cambio de programa en un lote) se envían los valores del panel, "_u8
                                      "para que el sonido sea siempre el mismo"_u8);
        sendOnStartToggle.setToggleState (true, juce::dontSendNotification);
        sendOnStartToggle.onClick = [this] { changed(); };
        addAndMakeVisible (sendOnStartToggle);

        clearButton.setButtonText ("Borrar todo");
        clearButton.onClick = [this] { controls.clear(); learnTarget = -1; rebuild(); changed(); };
        addAndMakeVisible (clearButton);

        statusLabel.setFont (juce::FontOptions (12.0f));
        statusLabel.setColour (juce::Label::textColourId, Colours::accent2);
        addAndMakeVisible (statusLabel);

        viewport.setViewedComponent (&holder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        startTimerHz (12);
        rebuild();
    }

    HardwareControlsPanel::~HardwareControlsPanel() = default;

    void HardwareControlsPanel::changed()
    {
        if (onChanged) onChanged();
    }

    void HardwareControlsPanel::rebuild()
    {
        views.clear();
        for (int i = 0; i < (int) controls.size(); ++i)
        {
            auto v = std::make_unique<ControlView> (*this, i);
            holder.addAndMakeVisible (*v);
            views.push_back (std::move (v));
        }
        updateStatus();
        resized();
    }

    void HardwareControlsPanel::updateStatus()
    {
        juce::String text;
        if (learning && learnTarget >= 0)
            text = "Mueve el mando de tu teclado MIDI para \""_u8 + controls[(size_t) learnTarget].name + "\"";
        else if (learning)
            text = "Aprendiendo: mueve los mandos de tu teclado MIDI"_u8 + (lastLearned.isNotEmpty() ? "  ·  añadido: " + lastLearned : juce::String())
                   + "  ·  pulsa «Aprender del teclado» para terminar"_u8;
        else if (controls.empty())
            text = "Añade controles (Cutoff, Resonancia...) o apréndelos de tu teclado MIDI. Se guardan para cada salida MIDI"_u8;
        else
            text = juce::String ((int) controls.size()) + " controles  ·  «...» o clic derecho: nombre, CC, canal, perilla o botón  ·  doble clic: aprender CC"_u8;
        statusLabel.setText (text, juce::dontSendNotification);
        learnButton.setToggleState (learning && learnTarget < 0, juce::dontSendNotification);
    }

    void HardwareControlsPanel::timerCallback()
    {
        for (auto& v : views)
            v->sync();
    }

    void HardwareControlsPanel::addControl (const juce::String& name, int cc, bool isButton)
    {
        Control c;
        c.name = name;
        c.cc = juce::jlimit (0, 127, cc);
        c.isButton = isButton;
        c.value = isButton ? 0 : 64;
        controls.push_back (c);
        rebuild();
        changed();
    }

    void HardwareControlsPanel::setValue (int index, int value, bool send)
    {
        if (! juce::isPositiveAndBelow (index, (int) controls.size()))
            return;
        auto& c = controls[(size_t) index];
        value = juce::jlimit (0, 127, value);
        const bool different = c.value != value;
        c.value = value;
        if (send && sendCc)
            sendCc (c.channel, c.cc, value);
        if (different)
            changed();
    }

    void HardwareControlsPanel::sendAll()
    {
        if (sendCc)
            for (auto& c : controls)
                sendCc (c.channel, c.cc, c.value);
    }

    std::vector<ControlValue> HardwareControlsPanel::getValuesToSend() const
    {
        std::vector<ControlValue> values;
        if (sendOnStartToggle.getToggleState())
            for (auto& c : controls)
                values.push_back ({ c.channel, c.cc, c.value });
        return values;
    }

    void HardwareControlsPanel::handleIncomingCc (int channel, int cc, int value)
    {
        juce::ignoreUnused (channel);

        if (learning)
        {
            if (learnTarget >= 0 && learnTarget < (int) controls.size())
            {
                controls[(size_t) learnTarget].cc = cc;
                controls[(size_t) learnTarget].value = value;
                learning = false;
                learnTarget = -1;
                rebuild();
                changed();
                return;
            }

            bool exists = false;
            for (auto& c : controls)
                exists = exists || c.cc == cc;
            if (! exists)
            {
                juce::String name = "CC " + juce::String (cc);
                bool isButton = cc >= 64 && cc <= 69;   // pedals and switches
                for (auto& common : commonCcs)
                    if (common.cc == cc) { name = juce::String::fromUTF8 (common.name); isButton = common.button; break; }
                lastLearned = name;
                addControl (name, cc, isButton);
                controls.back().value = value;
            }
        }

        // the controller already plays the instrument (MIDI thru): the panel just follows it
        for (int i = 0; i < (int) controls.size(); ++i)
            if (controls[(size_t) i].cc == cc)
                setValue (i, value, false);
    }

    void HardwareControlsPanel::showAddMenu()
    {
        juce::PopupMenu menu;
        int id = 1;
        for (auto& c : commonCcs)
            menu.addItem (id++, juce::String::fromUTF8 (c.name) + "  (CC " + juce::String (c.cc) + ")");

        juce::PopupMenu other;
        for (int group = 0; group < 8; ++group)
        {
            juce::PopupMenu sub;
            for (int cc = group * 16; cc < group * 16 + 16; ++cc)
                sub.addItem (1000 + cc, "CC " + juce::String (cc));
            other.addSubMenu ("CC " + juce::String (group * 16) + " - " + juce::String (group * 16 + 15), sub);
        }
        menu.addSeparator();
        menu.addSubMenu ("Otro CC", other);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton), [this] (int result)
        {
            const int count = (int) (sizeof (commonCcs) / sizeof (commonCcs[0]));
            if (result >= 1 && result <= count)
            {
                const auto& c = commonCcs[result - 1];
                addControl (juce::String::fromUTF8 (c.name), c.cc, c.button);
            }
            else if (result >= 1000)
            {
                addControl ("CC " + juce::String (result - 1000), result - 1000, false);
            }
        });
    }

    void HardwareControlsPanel::showControlMenu (int index, juce::Component& target)
    {
        if (! juce::isPositiveAndBelow (index, (int) controls.size()))
            return;
        const auto c = controls[(size_t) index];

        juce::PopupMenu menu;
        menu.addSectionHeader (c.name);
        menu.addItem (1, "Cambiar el nombre...");
        menu.addItem (2, "Aprender el CC: mueve un mando de tu teclado...");

        juce::PopupMenu ccMenu;
        for (int group = 0; group < 8; ++group)
        {
            juce::PopupMenu sub;
            for (int cc = group * 16; cc < group * 16 + 16; ++cc)
                sub.addItem (1000 + cc, "CC " + juce::String (cc), true, cc == c.cc);
            ccMenu.addSubMenu ("CC " + juce::String (group * 16) + " - " + juce::String (group * 16 + 15), sub, true,
                               juce::Image(), c.cc >= group * 16 && c.cc < group * 16 + 16);
        }
        menu.addSubMenu ("Elegir CC", ccMenu);

        juce::PopupMenu channelMenu;
        channelMenu.addItem (2000, "Canal del instrumento", true, c.channel == 0);
        for (int ch = 1; ch <= 16; ++ch)
            channelMenu.addItem (2000 + ch, "Canal " + juce::String (ch), true, c.channel == ch);
        menu.addSubMenu ("Canal MIDI", channelMenu);

        menu.addSeparator();
        menu.addItem (3, "Mostrar como perilla (0-127)", true, ! c.isButton);
        menu.addItem (4, "Mostrar como botón (0 / 127)"_u8, true, c.isButton);
        menu.addSeparator();
        menu.addItem (5, "Quitar");

        juce::Component::SafePointer<HardwareControlsPanel> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [self, index] (int result)
        {
            if (self == nullptr || ! juce::isPositiveAndBelow (index, (int) self->controls.size()))
                return;
            auto& panel = *self;
            auto& control = panel.controls[(size_t) index];

            if (result == 1)
            {
                auto* w = new juce::AlertWindow ("Nombre del control", {}, juce::MessageBoxIconType::NoIcon, &panel);
                w->addTextEditor ("name", control.name);
                w->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([self, index, w] (int r)
                {
                    const auto name = w->getTextEditorContents ("name").trim();
                    if (r == 1 && name.isNotEmpty() && self != nullptr && juce::isPositiveAndBelow (index, (int) self->controls.size()))
                    {
                        self->controls[(size_t) index].name = name;
                        self->changed();
                    }
                }), true);
                return;
            }

            if (result == 2) { panel.learning = true; panel.learnTarget = index; panel.updateStatus(); return; }
            if (result == 3) control.isButton = false;
            if (result == 4) { control.isButton = true; control.value = control.value >= 64 ? 127 : 0; }
            if (result == 5) panel.controls.erase (panel.controls.begin() + index);
            if (result >= 1000 && result < 1128) control.cc = result - 1000;
            if (result >= 2000 && result <= 2016) control.channel = result - 2000;
            if (result != 0)
            {
                panel.rebuild();
                panel.changed();
            }
        });
    }

    std::unique_ptr<juce::XmlElement> HardwareControlsPanel::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("HardwareControls");
        xml->setAttribute ("sendOnStart", sendOnStartToggle.getToggleState());
        for (auto& c : controls)
        {
            auto* e = xml->createNewChildElement ("Control");
            e->setAttribute ("name", c.name);
            e->setAttribute ("cc", c.cc);
            e->setAttribute ("channel", c.channel);
            e->setAttribute ("button", c.isButton);
            e->setAttribute ("value", c.value);
        }
        return xml;
    }

    void HardwareControlsPanel::fromXml (const juce::XmlElement* xml)
    {
        controls.clear();
        learning = false;
        learnTarget = -1;
        if (xml != nullptr)
        {
            sendOnStartToggle.setToggleState (xml->getBoolAttribute ("sendOnStart", true), juce::dontSendNotification);
            for (auto* e : xml->getChildWithTagNameIterator ("Control"))
            {
                Control c;
                c.name = e->getStringAttribute ("name", "CC");
                c.cc = juce::jlimit (0, 127, e->getIntAttribute ("cc", 74));
                c.channel = juce::jlimit (0, 16, e->getIntAttribute ("channel", 0));
                c.isButton = e->getBoolAttribute ("button", false);
                c.value = juce::jlimit (0, 127, e->getIntAttribute ("value", 64));
                controls.push_back (c);
            }
        }
        rebuild();
    }

    void HardwareControlsPanel::resized()
    {
        auto b = getLocalBounds();
        auto top = b.removeFromTop (26);
        addButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (6);
        learnButton.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (6);
        clearButton.setBounds (top.removeFromRight (86));
        top.removeFromRight (6);
        sendAllButton.setBounds (top.removeFromLeft (96));
        top.removeFromLeft (8);
        sendOnStartToggle.setBounds (top);

        b.removeFromTop (2);
        statusLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (2);
        viewport.setBounds (b);

        const int w = 104, gap = 6;
        const int h = juce::jmax (40, b.getHeight() - ((int) views.size() * (w + gap) > b.getWidth() ? 10 : 0));
        holder.setSize (juce::jmax (b.getWidth(), (int) views.size() * (w + gap)), h);
        for (size_t i = 0; i < views.size(); ++i)
            views[i]->setBounds ((int) i * (w + gap), 0, w, h);
    }
}

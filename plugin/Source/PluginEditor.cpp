#include "PluginEditor.h"
#include "LcdFont.h"
#include <cstring>

void LcdComponent::paint(juce::Graphics &g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1b2412)); g.fillRoundedRectangle(b, 6.0f);
    auto inner = b.reduced(10.0f);
    g.setColour(juce::Colour(0xffb5c86a)); g.fillRect(inner);

    // Dot-matrix layout: 16 cells of 5x8 dots, one blank dot between cells and rows, one around the edge.
    const int gridW = 16 * 6 + 1, gridH = 2 * 9 + 1;
    const float p = std::min(inner.getWidth() / gridW, inner.getHeight() / gridH);
    const float ox = inner.getX() + (inner.getWidth() - p * gridW) * 0.5f;
    const float oy = inner.getY() + (inner.getHeight() - p * gridH) * 0.5f;
    const float d = p * 0.88f, off = (p - d) * 0.5f;
    const juce::Colour on(0xff1f2a0c), ghost(0xff1f2a0c);

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 16; col++) {
            const uint8_t c = snap.ddram[(row ? 0x40 : 0) + col];
            uint8_t glyph[8] = {};
            if (c < 16)            std::memcpy(glyph, snap.cgram + (c & 7) * 8, 8);          // CGRAM (0-7 mirrored at 8-15)
            else if (c < 0x80)     std::memcpy(glyph, LcdFont::ascii[c < 0x20 ? 0 : c - 0x20], 8);
            else if (c >= 0xa0)    for (auto &r : glyph) r = 0x1f;                          // katakana etc.: not in our font
            const int cx = 1 + col * 6, cy = 1 + row * 9;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 5; x++) {
                    const bool lit = snap.displayOn && (glyph[y] & (0x10 >> x));
                    g.setColour(lit ? on : ghost.withAlpha(0.06f));
                    g.fillRect(ox + (cx + x) * p + off, oy + (cy + y) * p + off, d, d);
                }
        }
    }
}

TX81ZEditor::TX81ZEditor(TX81ZProcessor &p) : AudioProcessorEditor(&p), proc(p)
{
    setSize(600, 380);
    addAndMakeVisible(lcd);

    status.setJustificationType(juce::Justification::centredLeft);
    status.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(status);

    for (auto *b : { &loadRom, &powerCycleBtn, &factoryBtn }) addAndMakeVisible(*b);
    loadRom.onClick = [this] { chooseRom(); };
    powerCycleBtn.onClick = [this] { proc.powerCycle(); };
    factoryBtn.onClick = [this] {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Factory init").withMessage("This erases all voices, performances and settings in this instance.")
            .withButton("Erase and re-initialise").withButton("Cancel").withAssociatedComponent(this),
            [this](int r) { if (r == 1) proc.factoryInit(); });
    };

    static const char *names[Engine::NumKeys] = { "Play/Perform", "Edit/Compare", "Utility", "Store", "Inc", "Dec",
                                                  "Param >", "< Param", "Cursor", "Vol >", "< Vol" };
    for (int i = 0; i < Engine::NumKeys; i++) {
        keyBtns[i].setButtonText(names[i]);
        keyBtns[i].onStateChange = [this] { updateKeys(); };
        addAndMakeVisible(keyBtns[i]);
    }

    gain.setSliderStyle(juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    gain.setTextValueSuffix(" dB");
    addAndMakeVisible(gain);
    gainLabel.setText("Output gain", juce::dontSendNotification);
    gainLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(gainLabel);
    gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "gain", gain);

    startTimerHz(30);
    timerCallback();
}

TX81ZEditor::~TX81ZEditor() { proc.setKeyMask(0); }

void TX81ZEditor::updateKeys()
{
    uint32_t m = 0;
    for (int i = 0; i < Engine::NumKeys; i++) if (keyBtns[i].isDown()) m |= (1u << i);
    proc.setKeyMask(m);
}

void TX81ZEditor::chooseRom()
{
    chooser = std::make_unique<juce::FileChooser>("Select a TX81Z ROM image (64 KB, 27C512 .ic15)", juce::File(), "*.ic15;*.bin;*.rom");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser &fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (!proc.loadRomFile(f))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "ROM not loaded",
                                                       "Expected a 65536-byte TX81Z ROM image.", "OK", this);
        });
}

void TX81ZEditor::timerCallback()
{
    lcd.setSnapshot(proc.getLcd());
    status.setText(proc.hasRom() ? "ROM: " + juce::File(proc.getRomPath()).getFileName() : "No ROM loaded - click Load ROM...",
                   juce::dontSendNotification);
}

void TX81ZEditor::paint(juce::Graphics &g) { g.fillAll(juce::Colour(0xff23252b)); }

void TX81ZEditor::resized()
{
    auto r = getLocalBounds().reduced(14);
    lcd.setBounds(r.removeFromTop(120));
    r.removeFromTop(10);

    auto top = r.removeFromTop(28);
    loadRom.setBounds(top.removeFromLeft(110)); top.removeFromLeft(8);
    powerCycleBtn.setBounds(top.removeFromLeft(110)); top.removeFromLeft(8);
    factoryBtn.setBounds(top.removeFromLeft(110)); top.removeFromLeft(12);
    status.setBounds(top);
    r.removeFromTop(14);

    auto row = [&](std::initializer_list<int> ids) {
        auto line = r.removeFromTop(44);
        int w = (line.getWidth() - 8 * ((int)ids.size() - 1)) / (int)ids.size();
        for (int id : ids) { keyBtns[id].setBounds(line.removeFromLeft(w)); line.removeFromLeft(8); }
        r.removeFromTop(8);
    };
    row({ Engine::Play, Engine::Edit, Engine::Utility, Engine::Store });
    row({ Engine::ParamLeft, Engine::ParamRight, Engine::Dec, Engine::Inc });
    row({ Engine::VolLeft, Engine::VolRight, Engine::Cursor });

    r.removeFromTop(6);
    auto gl = r.removeFromTop(28);
    gainLabel.setBounds(gl.removeFromLeft(90));
    gain.setBounds(gl);
}

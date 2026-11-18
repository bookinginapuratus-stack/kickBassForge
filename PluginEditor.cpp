#include "PluginEditor.h"

KickBassEditor::KickBassEditor(KickBassProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    struct D { const char* id; const char* name; };
    static const D defs[] = {
        { "kStart", "K Start" }, { "kEnd", "K End" }, { "kPDecay", "Pitch Dec" }, { "kDecay", "K Decay" },
        { "kClick", "Click" }, { "kDrive", "K Drive" }, { "kLevel", "K Level" },
        { "bCut", "B Cutoff" }, { "bRes", "B Reso" }, { "bSub", "Sub" }, { "bDrive", "B Drive" }, { "bLevel", "B Level" },
        { "dDepth", "Duck Depth" }, { "dRel", "Duck Rel" }, { "varAmt", "Variacao" }, { "master", "Master" } };

    for (auto& d : defs)
    {
        auto k = std::make_unique<Knob>();
        k->s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        k->s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        k->l.setText(d.name, juce::dontSendNotification);
        k->l.setJustificationType(juce::Justification::centred);
        k->a = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, d.id, k->s);
        addAndMakeVisible(k->s); addAndMakeVisible(k->l);
        knobs.push_back(std::move(k));
    }
    addAndMakeVisible(loadBtn); addAndMakeVisible(varBtn);

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Escolha o JSON gerado por analyze.py", juce::File{}, "*.json");
        juce::Component::SafePointer<KickBassEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe != nullptr && f.existsAsFile()) safe->proc.loadAnalysis(f);
            });
    };
    varBtn.onClick = [this] { proc.generateVariation(); };
    setSize(760, 420);
}

void KickBassEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15171c));
    g.setColour(juce::Colours::white);
    g.setFont(20.f);
    g.drawText("KickBassForge", 16, 8, 300, 28, juce::Justification::left);
}

void KickBassEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    area.removeFromTop(32);
    auto top = area.removeFromTop(32);
    loadBtn.setBounds(top.removeFromLeft(200)); top.removeFromLeft(8);
    varBtn.setBounds(top.removeFromLeft(160));
    area.removeFromTop(8);
    const int cols = 8, rows = 2;
    const int w = area.getWidth() / cols, h = area.getHeight() / rows;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(area.getX() + (int)(i % cols) * w, area.getY() + (int)(i / cols) * h, w, h).reduced(4);
        knobs[i]->l.setBounds(cell.removeFromTop(18));
        knobs[i]->s.setBounds(cell);
    }
}

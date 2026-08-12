#include "PluginEditor.h"

BpmKeyDetectorEditor::BpmKeyDetectorEditor (BpmKeyDetectorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (360, 200);

    // Explicit text colours matter here: some hosts don't hand plugin editor
    // windows the same default LookAndFeel colours a standalone app gets,
    // and relying on the default (near-black) text colour on our dark
    // background can render as invisible - exactly the blank window you saw.
    const auto lightText = juce::Colour (0xffe8e8ec);

    statusLabel.setText ("No file selected", juce::dontSendNotification);
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setFont (juce::Font (14.0f));
    statusLabel.setColour (juce::Label::textColourId, lightText);
    statusLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff2a2a2e));
    statusLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff4a4a50));
    addAndMakeVisible (statusLabel);

    browseButton.onClick = [this] { openFileChooser(); };
    browseButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a40));
    browseButton.setColour (juce::TextButton::textColourOffId, lightText);
    addAndMakeVisible (browseButton);

    resultLabel.setText ("Ready", juce::dontSendNotification);
    resultLabel.setJustificationType (juce::Justification::centred);
    resultLabel.setFont (juce::Font (22.0f, juce::Font::bold));
    resultLabel.setColour (juce::Label::textColourId, lightText);
    addAndMakeVisible (resultLabel);

    creditLabel.setText ("made by FCK", juce::dontSendNotification);
    creditLabel.setJustificationType (juce::Justification::centredRight);
    creditLabel.setFont (juce::Font (11.0f));
    creditLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (creditLabel);

    startTimerHz (10);
}

BpmKeyDetectorEditor::~BpmKeyDetectorEditor()
{
    stopTimer();
}

void BpmKeyDetectorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e22));
}

void BpmKeyDetectorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    statusLabel.setBounds (area.removeFromTop (28));
    area.removeFromTop (10);

    browseButton.setBounds (area.removeFromTop (30));
    area.removeFromTop (16);

    auto creditRow = area.removeFromBottom (16);
    creditLabel.setBounds (creditRow);

    resultLabel.setBounds (area);
}

void BpmKeyDetectorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose an audio file to analyze",
        juce::File(),
        "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.caf;*.wma");

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return; // user cancelled - stay in whatever state we were in

        statusLabel.setText (file.getFileName(), juce::dontSendNotification);
        resultLabel.setText ("Analyzing...", juce::dontSendNotification);
        processor.analyzeFile (file);
    });
}

void BpmKeyDetectorEditor::timerCallback()
{
    if (processor.isAnalyzing())
        return; // resultLabel already shows "Analyzing..." from the click handler above

    auto result = processor.getLastResult();

    if (result.success)
        resultLabel.setText (juce::String (result.bpm, 1) + " BPM  -  " + result.keyName,
                              juce::dontSendNotification);
    else if (result.errorMessage.isNotEmpty())
        resultLabel.setText (result.errorMessage, juce::dontSendNotification);
    // else: no file has been analyzed yet - leave the initial "Ready" text as-is
}

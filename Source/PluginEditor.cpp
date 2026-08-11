#include "PluginEditor.h"

namespace
{
    const juce::StringArray acceptedExtensions = {
        ".wav", ".wave", ".aif", ".aiff", ".flac", ".ogg",
        ".mp3", ".m4a", ".aac", ".caf", ".wma"
    };

    bool hasAcceptedExtension (const juce::String& path)
    {
        auto lower = path.toLowerCase();
        for (auto& ext : acceptedExtensions)
            if (lower.endsWith (ext))
                return true;
        return false;
    }
}

BpmKeyDetectorEditor::BpmKeyDetectorEditor (BpmKeyDetectorProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (360, 180);

    dropZoneLabel.setText ("Drop an audio file here", juce::dontSendNotification);
    dropZoneLabel.setJustificationType (juce::Justification::centred);
    dropZoneLabel.setFont (juce::Font (16.0f));
    dropZoneLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff2a2a2e));
    dropZoneLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff4a4a50));
    addAndMakeVisible (dropZoneLabel);

    resultLabel.setText ({}, juce::dontSendNotification);
    resultLabel.setJustificationType (juce::Justification::centred);
    resultLabel.setFont (juce::Font (22.0f, juce::Font::bold));
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

    g.setColour (isDragOver ? juce::Colours::limegreen : juce::Colours::transparentBlack);
    g.drawRect (dropZoneLabel.getBounds(), 2);
}

void BpmKeyDetectorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    dropZoneLabel.setBounds (area.removeFromTop (80));
    area.removeFromTop (16);

    auto creditRow = area.removeFromBottom (16);
    creditLabel.setBounds (creditRow);

    resultLabel.setBounds (area);
}

bool BpmKeyDetectorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (hasAcceptedExtension (f))
            return true;
    return false;
}

void BpmKeyDetectorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    isDragOver = true;
    repaint();
}

void BpmKeyDetectorEditor::fileDragExit (const juce::StringArray&)
{
    isDragOver = false;
    repaint();
}

void BpmKeyDetectorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    isDragOver = false;

    for (auto& f : files)
    {
        if (hasAcceptedExtension (f))
        {
            dropZoneLabel.setText ("Analyzing " + juce::File (f).getFileName() + "...",
                                    juce::dontSendNotification);
            resultLabel.setText ({}, juce::dontSendNotification);
            processor.analyzeFile (juce::File (f));
            break; // only analyze the first accepted file
        }
    }

    repaint();
}

void BpmKeyDetectorEditor::timerCallback()
{
    if (processor.isAnalyzing())
        return;

    auto result = processor.getLastResult();

    if (result.success)
    {
        dropZoneLabel.setText ("Drop an audio file here", juce::dontSendNotification);
        resultLabel.setText (juce::String (result.bpm, 1) + " BPM   |   " + result.keyName,
                              juce::dontSendNotification);
    }
    else if (result.errorMessage.isNotEmpty())
    {
        dropZoneLabel.setText ("Drop an audio file here", juce::dontSendNotification);
        resultLabel.setText (result.errorMessage, juce::dontSendNotification);
    }
}

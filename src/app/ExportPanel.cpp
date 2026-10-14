#include "ExportPanel.h"

#include "Widgets.h"

namespace ng
{
namespace
{
    // The choices last used, so the dialog opens as it was left.
    struct LastChoices
    {
        int range = 1, format = 1, wavBits = 2, mp3Rate = 4, sampleRate = 1;
        bool stems = false;
    };

    LastChoices& lastChoices()
    {
        static LastChoices choices;
        return choices;
    }

    const int bitDepths[] = { 16, 24, 32 };
    const int bitRates[] = { 128, 192, 256, 320 };
    const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
}

ExportPanel::ExportPanel (Session& s, CommandDispatcher& d)
    : session (s), dispatcher (d), hasMp3Encoder (findMp3Encoder().existsAsFile())
{
    auto& last = lastChoices();
    auto hasLoop = session.getEdit().getTransport().getLoopRange().getLength() > te::TimeDuration();

    range.addItem ("The whole song", 1);
    range.addItem ("The loop region", 2);
    range.setItemEnabled (2, hasLoop);
    range.setSelectedId (hasLoop ? last.range : 1, juce::dontSendNotification);

    format.addItem ("WAV", 1);
    format.addItem (hasMp3Encoder ? "MP3" : "MP3 (needs LAME or FFmpeg)", 2);
    format.setItemEnabled (2, hasMp3Encoder);
    format.setSelectedId (hasMp3Encoder ? last.format : 1, juce::dontSendNotification);
    format.onChange = [this] { updateControls(); };

    sampleRate.addItem ("44.1 kHz", 1);
    sampleRate.addItem ("48 kHz", 2);
    sampleRate.addItem ("88.2 kHz", 3);
    sampleRate.addItem ("96 kHz", 4);
    sampleRate.setSelectedId (last.sampleRate, juce::dontSendNotification);

    stems.setToggleState (last.stems, juce::dontSendNotification);
    stems.setTooltip ("Each strip's channels through its own effects and fader, and each bus with what is sent to it. "
                      "The files all start at the same point, so they line up in another app.");

    note.setColour (juce::Label::textColourId, juce::Colours::grey);
    note.setFont (juce::FontOptions (12.0f));
    note.setJustificationType (juce::Justification::topLeft);

    exportButton.onClick = [this] { chooseDestination(); };
    cancelButton.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &rangeLabel, &formatLabel, &qualityLabel, &rateLabel, &note,
                                                             &range, &format, &quality, &sampleRate, &stems,
                                                             &exportButton, &cancelButton })
        addAndMakeVisible (c);

    updateControls();
    setSize (420, 250);
}

void ExportPanel::updateControls()
{
    auto& last = lastChoices();
    auto mp3 = format.getSelectedId() == 2;

    quality.clear (juce::dontSendNotification);
    quality.onChange = nullptr;

    if (mp3)
    {
        qualityLabel.setText ("Bit rate", juce::dontSendNotification);

        for (int i = 0; i < 4; ++i)
            quality.addItem (juce::String (bitRates[i]) + " kbps", i + 1);

        quality.setSelectedId (last.mp3Rate, juce::dontSendNotification);
    }
    else
    {
        qualityLabel.setText ("Bit depth", juce::dontSendNotification);
        quality.addItem ("16-bit", 1);
        quality.addItem ("24-bit", 2);
        quality.addItem ("32-bit float", 3);
        quality.setSelectedId (last.wavBits, juce::dontSendNotification);
    }

    note.setText (hasMp3Encoder ? "The song runs to the end of the last clip, plus two seconds for tails."
                                : "The song runs to the end of the last clip, plus two seconds for tails. "
                                  "For MP3, install LAME or FFmpeg and open this again.",
                  juce::dontSendNotification);
}

juce::File ExportPanel::getDefaultFolder() const
{
    auto project = session.getFile();
    return project.existsAsFile() ? project.getParentDirectory().getChildFile ("Exports")
                                  : juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                        .getChildFile ("Ng DAW").getChildFile ("Exports");
}

void ExportPanel::chooseDestination()
{
    auto& last = lastChoices();
    last.range = range.getSelectedId();
    last.format = format.getSelectedId();
    last.sampleRate = sampleRate.getSelectedId();
    last.stems = stems.getToggleState();
    (format.getSelectedId() == 2 ? last.mp3Rate : last.wavBits) = quality.getSelectedId();

    auto folder = getDefaultFolder();
    folder.createDirectory();
    auto extension = format.getSelectedId() == 2 ? ".mp3" : ".wav";

    if (stems.getToggleState())
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the stems", folder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis = juce::Component::SafePointer (this)] (const juce::FileChooser& fc)
                              {
                                  if (safeThis != nullptr && fc.getResult() != juce::File())
                                      safeThis->runExport (fc.getResult());
                              });
        return;
    }

    auto name = juce::File::createLegalFileName (session.getProjectName()) + (range.getSelectedId() == 2 ? " loop" : "");
    chooser = std::make_unique<juce::FileChooser> ("Export as", folder.getChildFile (name + extension), juce::String ("*") + extension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safeThis = juce::Component::SafePointer (this), extension] (const juce::FileChooser& fc)
                          {
                              if (safeThis != nullptr && fc.getResult() != juce::File())
                                  safeThis->runExport (fc.getResult().withFileExtension (extension));
                          });
}

void ExportPanel::runExport (const juce::File& destination)
{
    auto mp3 = format.getSelectedId() == 2;
    auto params = makeObject ({ { "path", destination.getFullPathName() },
                                { "format", mp3 ? "mp3" : "wav" },
                                { "range", range.getSelectedId() == 2 ? "loop" : "song" },
                                { "stems", stems.getToggleState() },
                                { "sample_rate", sampleRates[juce::jlimit (0, 3, sampleRate.getSelectedId() - 1)] } });

    if (mp3)
        params.getDynamicObject()->setProperty ("mp3_kbps", bitRates[juce::jlimit (0, 3, quality.getSelectedId() - 1)]);
    else
        params.getDynamicObject()->setProperty ("bit_depth", bitDepths[juce::jlimit (0, 2, quality.getSelectedId() - 1)]);

    // The export keeps handling messages while it renders. This dialog is modal, so nothing
    // else can be clicked meanwhile.
    auto* window = findParentComponentOfClass<juce::DialogWindow>();
    note.setText ("Exporting...", juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &range, &format, &quality, &sampleRate, &stems, &exportButton, &cancelButton })
        c->setEnabled (false);

    juce::MouseCursor::showWaitCursor();
    auto result = dispatcher.execute ("export_audio", params);
    juce::MouseCursor::hideWaitCursor();

    if (! result.ok)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &range, &format, &quality, &sampleRate, &stems, &exportButton, &cancelButton })
            c->setEnabled (true);

        updateControls();
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon, "Couldn't export",
                                                                             result.error.fromFirstOccurrenceOf ("export_audio: ", false, false),
                                                                             {}, this),
                                      nullptr);
        return;
    }

    auto files = result.value["files"];
    auto first = juce::File (files[0].toString());
    auto message = files.size() == 1 ? "Saved " + first.getFileName() + " in " + first.getParentDirectory().getFullPathName() + "."
                                     : "Saved " + juce::String (files.size()) + " stems in " + first.getParentDirectory().getFullPathName() + ".";

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("Exported")
                                      .withMessage (message)
                                      .withButton ("Show")
                                      .withButton ("OK"),
                                  [first] (int button)
                                  {
                                      if (button == 1)
                                          first.revealToUser();
                                  });

    if (window != nullptr)
        window->exitModalState (1);   // closes the dialog, which deletes this panel
}

void ExportPanel::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto row = [&r] (juce::Label& label, juce::Component& control)
    {
        auto line = r.removeFromTop (26);
        label.setBounds (line.removeFromLeft (100));
        control.setBounds (line);
        r.removeFromTop (8);
    };

    row (rangeLabel, range);
    row (formatLabel, format);
    row (qualityLabel, quality);
    row (rateLabel, sampleRate);
    stems.setBounds (r.removeFromTop (24));

    auto buttons = r.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (8);
    exportButton.setBounds (buttons.removeFromRight (90));
    note.setBounds (r.reduced (0, 4));
}

} // namespace ng

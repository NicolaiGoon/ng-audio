#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The Export dialog: the song or the loop region, as one WAV or MP3 file or as stems (one
    file per mixer strip), at a chosen bit depth or bit rate and sample rate. It runs
    export_audio, like an AI client would.
*/
class ExportPanel  : public juce::Component
{
public:
    ExportPanel (Session&, CommandDispatcher&);

    void resized() override;

private:
    void updateControls();
    void chooseDestination();
    void runExport (const juce::File&);
    juce::File getDefaultFolder() const;

    Session& session;
    CommandDispatcher& dispatcher;

    juce::Label rangeLabel { {}, "Export" }, formatLabel { {}, "Format" }, qualityLabel { {}, "Bit depth" },
                rateLabel { {}, "Sample rate" }, note;
    juce::ComboBox range, format, quality, sampleRate;
    juce::ToggleButton stems { "One file per mixer strip (stems)" };
    juce::TextButton exportButton { "Export..." }, cancelButton { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool hasMp3Encoder = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};

} // namespace ng

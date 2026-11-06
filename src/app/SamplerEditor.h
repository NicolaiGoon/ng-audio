#pragma once

#include "core/CommandDispatcher.h"

namespace ng
{

/**
    The sampler's window: its samples, one row each, with the key each plays on, its level
    and whether it plays to its end (one-shot). Samples are added with the buttons or by
    dropping audio files, or a kit's folder, onto the window. Every change runs a sampler
    command, so it's undoable like any other.
*/
std::unique_ptr<te::Plugin::EditorComponent> createSamplerEditor (te::SamplerPlugin&, CommandDispatcher&);

} // namespace ng

/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 lockedin_core
  vendor:             Locked In
  version:            0.2.0
  name:               Locked In core
  description:        Shared foundation for Locked In character plugins: MeterBus,
                      CharacterView, GagLayer, gag sounds, TransportRandom, params,
                      presets, LookAndFeel and the fixed-size editor base.
  website:            https://lockedinthestudio.com
  license:            Proprietary
  minimumCppStandard: 17

  dependencies:       juce_core juce_audio_basics juce_audio_formats juce_audio_processors
                      juce_data_structures juce_events juce_graphics juce_gui_basics

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once
#define LOCKEDIN_CORE_H_INCLUDED

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

// Headless pieces (only need juce_core / juce_audio_basics; unit-tested on their own)
#include "meters/MeterBus.h"
#include "meters/Levels.h"
#include "random/TransportRandom.h"
#include "character/CharacterDef.h"
#include "character/CharacterBrain.h"

// Plugin-side pieces
#include "assets/Assets.h"
#include "params/Params.h"
#include "audio/GagSounds.h"
#include "presets/PresetManager.h"
#include "processor/Processor.h"

// UI pieces
#include "look/LookAndFeel.h"
#include "character/CharacterView.h"
#include "gags/GagLayer.h"
#include "presets/PresetBar.h"
#include "editor/Editor.h"

#ifdef LOCKEDIN_CORE_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using. */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "lockedin_core.h"

#include "character/CharacterDef.cpp"
#include "character/CharacterBrain.cpp"
#include "character/CharacterView.cpp"
#include "audio/GagSounds.cpp"
#include "presets/PresetManager.cpp"
#include "presets/PresetBar.cpp"
#include "processor/Processor.cpp"
#include "look/LookAndFeel.cpp"
#include "gags/GagLayer.cpp"
#include "editor/Editor.cpp"

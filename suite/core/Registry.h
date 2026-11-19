#pragma once
#include "Module.h"

namespace sf
{
ModulePtr makeModule (const std::string& id);
std::vector<std::string> allModuleIds();

// Factory presets for a module (Source/modules/Presets.cpp). Empty for setup modules.
std::vector<Preset> factoryPresets (const std::string& id);
// KEY: semitone offset that moves a part written in C to the SF CONTROL key (nearest: -6..+5).
inline int keyOffset (int keyRoot) { return keyRoot < 0 ? 0 : (keyRoot > 5 ? keyRoot - 12 : keyRoot); }

// Factories (one per module, defined in Source/modules/*.cpp)
ModulePtr makeFILTER();  ModulePtr makeGATE();    ModulePtr makeDENSITY(); ModulePtr makeDELAY();
ModulePtr makeREVERB();  ModulePtr makeREPEAT();  ModulePtr makeDRIVE();   ModulePtr makeRADIO();
ModulePtr makeDUCK();    ModulePtr makeENERGY();  ModulePtr makeISOLATOR(); ModulePtr makeBUILD();
ModulePtr makeDRUMBUS();     // (SF-000: its own drum bus, modules/FxDrums.cpp)
ModulePtr makeGLUE();    ModulePtr makeLOWEND();  ModulePtr makeLIMIT();   ModulePtr makeROOMEQ();
ModulePtr makeFINISH();     // (SF-000)
// (SF-000) What FINISH says of itself to whoever holds it: the loudest three seconds of the last minute at its input (LUFS),
// the input it has found for itself (dB), and the most any of its bands was held in the last block (dB).
struct FinishSays { virtual ~FinishSays() = default; virtual float finishLoud() const = 0; virtual float finishAuto() const = 0; virtual float finishHeld() const = 0; };
ModulePtr makeCONTROL(); ModulePtr makeALLOC();   ModulePtr makeTAP();     ModulePtr makeSTRIP();
ModulePtr makeKICK();    ModulePtr makePERC();    ModulePtr makeRUMBLE();  ModulePtr makeBASS();
ModulePtr makeLEAD();    ModulePtr makePAD();     ModulePtr makeVOICE();   ModulePtr makeDECK();
ModulePtr makeTONE();    ModulePtr makeSTRIKE();  ModulePtr makeTERROIR(); ModulePtr makeCAPTURE();
ModulePtr makeRESONATOR();
// (SF-000, the ninety-first build) Airwindows' (modules/Airwindows.cpp)
ModulePtr makeAIRWEAVE();  ModulePtr makeAIRDEREZ(); ModulePtr makeAIRFILTER(); ModulePtr makeAIRENSEMBLE();
ModulePtr makeAIRTAPE();   ModulePtr makeAIRCLIP();  ModulePtr makeAIRGALAXY();
} // namespace sf

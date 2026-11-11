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
ModulePtr makeGLUE();    ModulePtr makeLOWEND();  ModulePtr makeLIMIT();   ModulePtr makeROOMEQ();
ModulePtr makeCONTROL(); ModulePtr makeALLOC();   ModulePtr makeTAP();     ModulePtr makeSTRIP();
ModulePtr makeKICK();    ModulePtr makePERC();    ModulePtr makeRUMBLE();  ModulePtr makeBASS();
ModulePtr makeLEAD();    ModulePtr makePAD();     ModulePtr makeVOICE();   ModulePtr makeDECK();
ModulePtr makeTONE();    ModulePtr makeSTRIKE();  ModulePtr makeTERROIR(); ModulePtr makeCAPTURE();
ModulePtr makeRESONATOR();
} // namespace sf

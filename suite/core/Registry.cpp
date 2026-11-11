#include "Registry.h"
#include <map>

namespace sf
{
static const std::vector<std::pair<std::string, ModulePtr (*)()>>& table()
{
    static const std::vector<std::pair<std::string, ModulePtr (*)()>> t {
        { "KICK", makeKICK }, { "PERC", makePERC }, { "RUMBLE", makeRUMBLE }, { "BASS", makeBASS }, { "LEAD", makeLEAD },
        { "PAD", makePAD }, { "VOICE", makeVOICE }, { "STRIKE", makeSTRIKE }, { "DECK", makeDECK }, { "CONTROL", makeCONTROL },
        { "FILTER", makeFILTER }, { "GATE", makeGATE }, { "DENSITY", makeDENSITY }, { "DELAY", makeDELAY },
        { "REVERB", makeREVERB }, { "DRIVE", makeDRIVE }, { "RADIO", makeRADIO }, { "REPEAT", makeREPEAT },
        { "DUCK", makeDUCK }, { "ENERGY", makeENERGY }, { "ISOLATOR", makeISOLATOR }, { "BUILD", makeBUILD },
        { "GLUE", makeGLUE }, { "TONE", makeTONE }, { "LOWEND", makeLOWEND }, { "LIMIT", makeLIMIT }, { "ROOMEQ", makeROOMEQ },
        { "ALLOC", makeALLOC }, { "TAP", makeTAP }, { "STRIP", makeSTRIP }, { "TERROIR", makeTERROIR }, { "CAPTURE", makeCAPTURE },
        { "RESONATOR", makeRESONATOR } };
    return t;
}

ModulePtr makeModule (const std::string& id)
{
    for (auto& e : table()) if (e.first == id) return e.second();
    jassertfalse;
    return makeFILTER();
}

std::vector<std::string> allModuleIds()
{
    std::vector<std::string> v;
    for (auto& e : table()) v.push_back (e.first);
    return v;
}
} // namespace sf

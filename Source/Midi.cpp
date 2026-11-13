#include "Midi.h"
#include "BinaryData.h"
#include <chrono>
#include <map>
#include <thread>
#include <string>

namespace sf0
{
using juce::String;

// ============================================================================== the addresses
namespace e16
{
int slotFor (int channel, int number)
{
    if (channel >= 1 && channel <= kLanes)
    {
        if (number >= kKnobCc && number < kKnobCc + 16) return (channel - 1) * kPerLane + (number - kKnobCc);
        if (number == kMidCc) return (channel - 1) * kPerLane + 16;
    }
    else if (channel == kSnapChannel && number >= kKnobCc && number < kKnobCc + 16)
        return kLanes * kPerLane + (number - kKnobCc);
    return -1;
}

int controlFor (int slot)
{
    if (slot >= kLanes * kPerLane) return cx::kSnap + (slot - kLanes * kPerLane);
    const int l = slot / kPerLane, j = slot % kPerLane;
    static const int tail[] = { cx::kSendA, cx::kSendB, cx::kEqLow, cx::kEqHigh, cx::kEqMid };
    return cx::lane (l, j < kLaneKnobs ? j : tail[j - kLaneKnobs]);
}

void addressOf (int slot, int& channel, int& number)
{
    if (slot >= kLanes * kPerLane) { channel = kSnapChannel; number = kKnobCc + (slot - kLanes * kPerLane); return; }
    const int j = slot % kPerLane;
    channel = slot / kPerLane + 1;
    number = j == 16 ? kMidCc : kKnobCc + j;
}

// A knob with a centre (TUNE, FILTER, the EQ) sits there at 64 exactly: 0-64 is its lower half and
// 64-127 its upper. Everything else is the plain 0-127.
static double pos7 (const Control& c, float v)
{
    const double x = juce::jlimit (0.0, 1.0, (double) v);
    if (c.kind == Kind::Bipolar) return x <= 0.5 ? x * 128.0 : 64.0 + (x - 0.5) * 126.0;
    return x * 127.0;
}

int to7 (const Control& c, float v) { return juce::jlimit (0, 127, (int) std::lround (pos7 (c, v))); }

float from7 (const Control& c, int v)
{
    v = juce::jlimit (0, 127, v);
    if (c.kind == Kind::Bipolar) return v <= 64 ? (float) v / 128.0f : 0.5f + (float) (v - 64) / 126.0f;
    return (float) v / 127.0f;
}

} // namespace e16

// ------------------------------------------------------------- the addresses not on the E16's scene
namespace ccx
{
int extraFor (int ch, int num)
{
    if (ch >= 1 && ch <= kLanes)
    {
        if (num >= kIns && num < kIns + kSlots * kCells) return (ch - 1) * kPerLane + (num - kIns);
        if (num >= kMute && num <= kSeqMute) return (ch - 1) * kPerLane + kSlots * kCells + (num - kMute);
    }
    else if (ch == e16::kSnapChannel && num >= kDub && num < kMaster + 3) return kLanes * kPerLane + (num - kDub);
    return -1;
}

int controlFor (int extra, int spaceModel)
{
    if (extra >= kLanes * kPerLane)
    {
        const int j = extra - kLanes * kPerLane;
        return j < 4 ? cx::kRet + j : j < 8 ? cx::space (spaceModel, j - 4) : cx::kMaster + (j - 8);
    }
    const int l = extra / kPerLane, j = extra % kPerLane;
    if (j < kSlots * kCells) return cx::ins (l, j / kCells, j % kCells);
    static const int tail[] = { cx::kMute, cx::kSolo, cx::kSeqMute };
    return l * cx::kPerLane + tail[j - kSlots * kCells];
}

bool addressOf (int ctl, int& ch, int& num)
{
    if (ctl < 0 || ctl >= cx::kCount) return false;
    ch = e16::kSnapChannel;
    if (ctl >= cx::kSnap) { num = e16::kKnobCc + (ctl - cx::kSnap); return true; }
    if (ctl == cx::kGlide) { num = e16::kGlideCc; return true; }
    if (ctl >= cx::kMaster && ctl < cx::kMaster + 3) { num = kMaster + (ctl - cx::kMaster); return true; }
    if (ctl >= cx::kRet && ctl < cx::kRet + cx::kRetKnobs) { const int j = ctl - cx::kRet; num = j < 4 ? kDub + j : kSpace + (j - 4) % 4; return true; }
    if (ctl >= cx::kRet) return false;
    const int j = ctl % cx::kPerLane;
    ch = ctl / cx::kPerLane + 1;
    if (j < kLaneKnobs) num = e16::kKnobCc + j;
    else if (j == cx::kSendA) num = e16::kKnobCc + 12;
    else if (j == cx::kSendB) num = e16::kKnobCc + 13;
    else if (j == cx::kEqLow) num = e16::kKnobCc + 14;
    else if (j == cx::kEqHigh) num = e16::kKnobCc + 15;
    else if (j == cx::kEqMid) num = e16::kMidCc;
    else if (j >= cx::kIns && j < cx::kIns + kSlots * kCells) num = kIns + (j - cx::kIns);
    else if (j >= cx::kGen && j < cx::kGen + 4) { ch = e16::kFxChannel; num = e16::kGenCc + (j - cx::kGen); }       // (the generator's knobs: the active lane's, on the channel of what is in front)
    else if (j == cx::kMute) num = kMute;
    else if (j == cx::kSolo) num = kSolo;
    else if (j == cx::kSeqMute) num = kSeqMute;
    else return false;
    return true;
}
} // namespace ccx

namespace e16
{
// ------------------------------------------------------------------------------- the scene file
// Written as the OXI App 2.x exports a scene (learned by SF Live Suite's controllers/e16/make_scene.py
// from the App's own files): every key in the App's order, compact, ASCII. Turns are CC Abs (type 3)
// with the number in nr1; mode 14 follows the scene's acceleration. A hold press is a CC (type 2,
// Press & Release, 127 down and 0 up); a swap press (type 9) is the App's own, as exported, and the
// second destination is turn action 2 with color2 as its ring.
namespace
{
struct Json
{
    std::string s;
    static std::string str (const String& in)
    {
        std::string o = "\"";
        for (auto t = in.getCharPointer(); ! t.isEmpty();)
        {
            const auto ch = (unsigned) t.getAndAdvance();
            if (ch == '"') o += "\\\"";
            else if (ch == '\\') o += "\\\\";
            else if (ch == '\n') o += "\\n";
            else if (ch == '\r') o += "\\r";
            else if (ch == '\t') o += "\\t";
            else if (ch < 0x20 || ch > 0x7e) { char b[8]; std::snprintf (b, sizeof (b), "\\u%04x", ch & 0xffffu); o += b; }
            else o += (char) ch;
        }
        return o + "\"";
    }
    static std::string obj (std::initializer_list<std::pair<const char*, std::string>> kv)
    {
        std::string o = "{";
        bool first = true;
        for (auto& p : kv) { if (! first) o += ","; first = false; o += "\""; o += p.first; o += "\":"; o += p.second; }
        return o + "}";
    }
    static std::string arr (const std::vector<std::string>& v)
    {
        std::string o = "[";
        for (size_t i = 0; i < v.size(); ++i) { if (i > 0) o += ","; o += v[i]; }
        return o + "]";
    }
    static std::string num (int v) { return std::to_string (v); }
};

struct Enc
{
    String name { "-" }, abbr { "-" };
    int colour = kWhite, channel = 0, number = -1, def = 0;     // number -1: nothing on it
    int press = 0;                                              // 0 off, 1 hold (a CC), 2 swap to the second destination
    int pressChannel = 0, pressNumber = 0;
    int channel2 = 0, number2 = 0, def2 = 0, colour2 = 0;
};

std::string turnJson (int type, int mode, int channel, int def, int nr1)
{
    return Json::obj ({ { "instrument", "127" }, { "parameter", "0" }, { "type", Json::num (type) }, { "display", "10" }, { "mode", Json::num (mode) },
                        { "channel", Json::num (channel) }, { "lower", "0" }, { "upper", "127" }, { "defaultValue", Json::num (def) },
                        { "nr1", Json::num (nr1) }, { "nr2", "0" }, { "output", "12" }, { "scriptId", "0" } });
}

std::string pushJson (int type, int mode, int channel, int lower, int upper, int nr1, int nr2, int output)
{
    return Json::obj ({ { "instrument", "127" }, { "parameter", "0" }, { "type", Json::num (type) }, { "display", "0" }, { "mode", Json::num (mode) },
                        { "channel", Json::num (channel) }, { "lower", Json::num (lower) }, { "upper", Json::num (upper) },
                        { "nr1", Json::num (nr1) }, { "nr2", Json::num (nr2) }, { "output", Json::num (output) }, { "scriptId", "0" } });
}

std::string encJson (const Enc& e, int place)
{
    const bool blank = e.number < 0;
    const std::string push = e.press == 1 ? pushJson (2, 1, e.pressChannel, 0, 127, e.pressNumber, place, 0)
                           : e.press == 2 ? pushJson (9, 0, 0, 0, 0, 17, 11, 15)
                                          : pushJson (0, 0, 0, 0, 0, place, place, 0);
    const std::string t1 = blank ? turnJson (0, 14, 0, 0, place) : turnJson (3, 14, e.channel, e.def, e.number);
    const std::string t2 = e.press == 2 ? turnJson (3, 14, e.channel2, e.def2, e.number2) : turnJson (0, 3, 0, 0, 0);
    return Json::obj ({ { "name", Json::str (e.name) }, { "abbr", Json::str (e.abbr) }, { "color", Json::num (e.colour) }, { "push_action", push },
                        { "turn_actions", Json::arr ({ t1, t2 }) }, { "color2", Json::num (e.press == 2 ? e.colour2 : 0) } });
}

const char* const laneCodes[kLanes] = { "KCK", "CHH", "OHH", "CLP", "PRC", "BAS", "RMB", "LED", "CRD", "PAD", "VOX" };

// A knob's name on the E16: 8 characters at most, and 4 for its short form.
String cleaned (String s, int len)
{
    String o;
    const String upper = s.toUpperCase();
    for (auto t = upper.getCharPointer(); ! t.isEmpty();)
    {
        const auto ch = t.getAndAdvance();
        if (ch >= 0x20 && ch <= 0x7e && ch != '"' && ch != '\\') o << String::charToString (ch);
    }
    o = o.trim().substring (0, len).trim();
    return o.isEmpty() ? String ("-") : o;
}

void naming (const KnobDef& k, String& name, String& abbr)
{
    static const std::map<std::string, const char*> shortName = {
        { "pitch_time", "PITCH T" }, { "echo_space", "ECHO SPC" }, { "echo_swing", "ECHO SWG" }, { "echo_prob", "ECHO PRB" },
        { "pocket_depth", "PCKT DEP" }, { "pocket_shape", "PCKT SHP" }, { "echo_time", "ECHO TME" }, { "echo_fb", "ECHO FBK" } };
    static const std::map<std::string, const char*> shortest = {
        { "cutoff", "CUT" }, { "res", "RES" }, { "env_amt", "ENV" }, { "gate", "GATE" }, { "density", "DENS" }, { "filter", "FILT" },
        { "drive", "DRV" }, { "level", "LVL" }, { "tune", "TUNE" }, { "decay", "DCY" }, { "punch", "PNCH" }, { "click", "CLCK" },
        { "size", "SIZE" }, { "dark", "DARK" }, { "grit", "GRIT" }, { "pump", "PUMP" }, { "tone", "TONE" }, { "power", "POWR" },
        { "distance", "DIST" }, { "material", "MATL" }, { "space", "SPC" }, { "color", "COL" }, { "detune", "DTUN" },
        { "attack", "ATK" }, { "signal", "SIG" }, { "echo", "ECHO" }, { "accent", "ACNT" }, { "glide", "GLID" }, { "pitch_time", "PTIM" },
        { "layer", "LAYR" }, { "echo_space", "ESPC" }, { "echo_swing", "ESWG" }, { "echo_prob", "EPRB" }, { "spread", "SPRD" },
        { "position", "POS" }, { "tension", "TENS" }, { "pocket_depth", "PCKT" }, { "pocket_shape", "PSHP" }, { "predelay", "PRE" },
        { "width", "WIDE" }, { "echo_time", "ETIM" }, { "echo_fb", "EFBK" },
        { "feedback", "FDBK" }, { "throw", "THRW" }, { "freeze", "FRZ" }, { "release", "REL" }, { "squelch", "SQLC" }, { "noise", "NOIS" } };
    const std::string id (k.param != nullptr ? k.param : "");
    const String label (k.label != nullptr ? k.label : "-");
    const auto sn = shortName.find (id);
    name = label.length() <= 8 ? cleaned (label, 8) : sn != shortName.end() ? String (sn->second) : cleaned (label, 8);
    const auto sa = shortest.find (id);
    abbr = sa != shortest.end() ? String (sa->second) : cleaned (label.removeCharacters (" "), 4);
}

// Comments and line breaks out, one line, as the App uploads a script. The script has double-quoted
// strings only, so a comment starts at a -- outside one.
String minified (const String& src)
{
    juce::StringArray out;
    for (auto line : juce::StringArray::fromLines (src))
    {
        int cut = line.length();
        bool quote = false;
        for (int i = 0; i < line.length(); ++i)
        {
            if (line[i] == '"') quote = ! quote;
            else if (! quote && line[i] == '-' && line[i + 1] == '-') { cut = i; break; }
        }
        line = line.substring (0, cut).trim();
        if (line.isNotEmpty()) out.add (line);
    }
    return out.joinIntoString (" ") + "\n";
}
} // namespace

String sceneText (const Engine& eng, const juce::StringArray& snapNames)
{
    const auto& codes = laneCodes;
    std::vector<std::string> pages;
    String map;
    juce::StringArray focus;
    auto hex = [] (int channel, int number) { return String::toHexString (channel - 1) + String::toHexString (number).paddedLeft ('0', 2); };

    for (int l = 0; l < kLanes; ++l)
    {
        const auto& d = lanes()[(size_t) l];
        std::vector<std::string> encs;
        for (int p = 1; p <= 16; ++p)
        {
            Enc e;
            const int slot = l * kPerLane + (p - 1), ctl = controlFor (slot);
            e.channel = l + 1; e.number = kKnobCc + p - 1;
            e.def = to7 (eng.control (ctl), eng.control (ctl).def);
            if (p <= kLaneKnobs)
            {
                naming (d.knobs[p - 1], e.name, e.abbr);
                e.colour = p <= 8 ? kWhite : p <= 11 ? kRed : kLightBlue;     // as the screen lights them: sound, shape, level
                if (p == 1)
                {
                    // and GLIDE behind a press: the ring turns green while the knob is on GLIDE
                    e.press = 2; e.channel2 = kSnapChannel; e.number2 = kGlideCc; e.colour2 = kGreen;
                    e.def2 = to7 (eng.control (cx::kGlide), eng.control (cx::kGlide).def);
                }
            }
            // (SEND A pressed is the pointer's wheel, SEND B pressed is OPEN: SF-000 is told, and does the rest)
            else if (p == 13) { e.name = "SEND A"; e.abbr = "SNDA"; e.colour = kLightBlue; e.press = 1; e.pressChannel = kSnapChannel; e.pressNumber = kWheelKey; }
            else if (p == 14) { e.name = "SEND B"; e.abbr = "SNDB"; e.colour = kLightBlue; e.press = 1; e.pressChannel = kSnapChannel; e.pressNumber = kOpenKey; }
            else if (p == 15) { e.name = "LOW"; e.abbr = "LOW"; e.colour = kLightBlue; }
            else
            {
                // HIGH, and MID behind a press: the ring turns pink while the knob is on MID
                const int mid = controlFor (l * kPerLane + 16);
                e.name = "HIGH/MID"; e.abbr = "HI/M"; e.colour = kLightBlue;
                e.press = 2; e.channel2 = l + 1; e.number2 = kMidCc; e.colour2 = kPink;
                e.def2 = to7 (eng.control (mid), eng.control (mid).def);
            }
            encs.push_back (encJson (e, p));
            map << (p == 16 || p == 1 ? String ("---") : hex (e.channel, e.number));     // a knob that swaps keeps its own ring
        }
        pages.push_back (Json::obj ({ { "title", Json::str (codes[l]) }, { "output", "0" }, { "channel", Json::num (l + 1) }, { "encoders", Json::arr (encs) } }));
        focus.add (String (l + 1));
    }
    {
        // page 12: the sixteen snapshot knobs; a press recalls the knob's nearest snapshot
        std::vector<std::string> encs;
        for (int p = 1; p <= 16; ++p)
        {
            Enc e;
            const String given = cleaned (snapNames[p - 1], 8) == "-" ? String() : snapNames[p - 1];
            e.name = given.isNotEmpty() ? cleaned (given, 8) : "SNAP " + String (p);
            e.abbr = given.isNotEmpty() ? cleaned (given.removeCharacters (" "), 4) : "S" + String (p);
            e.colour = kViolet;
            e.channel = kSnapChannel; e.number = kKnobCc + p - 1; e.def = 0;
            e.press = 1; e.pressChannel = kSnapChannel; e.pressNumber = kPressCc + p - 1;
            encs.push_back (encJson (e, p));
            map << "---";
        }
        pages.push_back (Json::obj ({ { "title", Json::str ("SNAP") }, { "output", "0" }, { "channel", Json::num (kSnapChannel) }, { "encoders", Json::arr (encs) } }));
        focus.add (String (kSnapPage));         // (not a lane: SF-000 leaves the active lane alone, and tells the page its values)
    }

    String src = String::fromUTF8 (BinaryData::rings_lua, BinaryData::rings_luaSize).replace ("\r\n", "\n");
    src = src.replace ("@MAP@", map).replace ("@GREEN@", String (kGreen)).replace ("@FOCUS@", focus.joinIntoString (", "))
             .replace ("@FOCUS_CH@", String (kSnapChannel - 1)).replace ("@FOCUS_CC@", String (kFocusCc));

    // the icon: 16 x 16, a bit a pixel, two bytes a row. Three noughts.
    std::vector<std::string> icon;
    for (int r = 0; r < 16; ++r)
    {
        int row = 0;
        for (int c = 0; c < 16; ++c)
        {
            const int x = c % 5, zero = c / 5;
            const bool lit = zero < 3 && r >= 4 && r <= 11 && x >= 1 && x <= 4 && (r == 4 || r == 11 || x == 1 || x == 4);
            if (lit) row |= 1 << (15 - c);
        }
        icon.push_back (Json::num (row >> 8)); icon.push_back (Json::num (row & 0xff));
    }
    const std::string code = Json::obj ({ { "code", Json::str (minified (src)) }, { "fullScript", Json::str (src.replace ("\n", "\r\n")) },
                                          { "scriptName", Json::str ("sf_glide_rings") } });
    const std::string scene = Json::obj ({
        { "title", Json::str ("SF-000") }, { "icon", Json::arr (icon) }, { "selectedPreset", "0" }, { "color", Json::num (kWhite) },
        { "transmitMode", "0" }, { "pcOnEntry", "0" }, { "bankOnEntry", "65535" }, { "recentPage", "0" }, { "recentPreset", "0" },
        { "transmitOnSceneEntry", "0" }, { "transmitOnPageSwitch", "0" }, { "transmitOnPresetLoad", "0" }, { "smartTransmit", "15" },
        { "outputOnEntry", "0" }, { "holdMode", "0" }, { "acceleration", "3" }, { "code", code }, { "pages", Json::arr (pages) } });
    return String (scene);
}

// The second scene: the library's map and finders, then a page for everything the first scene has
// none for (the active lane's inserts, the returns, the master, the sequencer strip, CONFIG). It
// carries the same script, which says each page as it comes up and takes the names SF-000 sends.
String fxText (const Engine& eng)
{
    std::vector<std::string> pages;
    String map;
    juce::StringArray focus;
    static const char* const rows[4] = { "PEAK", "DRIVE", "EASY", "CALM" };         // the top row of knobs first
    static const char* const cols[4] = { "DK", "WM", "CL", "BR" };                  // dark, warm, clear, bright
    static const int colColour[4] = { kRed, kPink, kLightBlue, kWhite };
    auto add = [&] (const String& title, int channel, const std::vector<Enc>& encs)
    {
        std::vector<std::string> js;
        for (int p = 1; p <= 16; ++p) { js.push_back (encJson (p <= (int) encs.size() ? encs[(size_t) (p - 1)] : Enc(), p)); map << "---"; }
        pages.push_back (Json::obj ({ { "title", Json::str (title) }, { "output", "0" }, { "channel", Json::num (channel) }, { "encoders", Json::arr (js) } }));
        focus.add (String (kFocusB + (int) pages.size()));
    };
    // the bottom row these pages share: 13 the pointer's wheel (its press makes it so on the pages where it is something else), 14 OPEN
    auto wheel = []
    {
        Enc e; e.name = "SELECT"; e.abbr = "SEL"; e.colour = kPink;
        e.channel = kSnapChannel; e.number = kWheelCc; e.def = 0;
        e.press = 1; e.pressChannel = kSnapChannel; e.pressNumber = kWheelKey;
        return e;
    };
    auto key = [] (const char* name, const char* abbr, int number)
    {
        Enc e; e.name = name; e.abbr = abbr; e.colour = kPink;
        e.press = 1; e.pressChannel = kSnapChannel; e.pressNumber = number;
        return e;
    };
    {
        std::vector<Enc> encs (16);
        for (int p = 1; p <= 16; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            const int r = (p - 1) / 4, c = (p - 1) % 4;
            e.name = String (rows[r]) + " " + cols[c]; e.abbr = String (rows[r]).substring (0, 2) + cols[c];
            e.colour = colColour[c];
            e.channel = kMapChannel; e.number = kKnobCc + p - 1; e.def = 0;
            e.press = 1; e.pressChannel = kMapChannel; e.pressNumber = kPressCc + p - 1;
        }
        add ("MAP", kMapChannel, encs);
    }
    {
        static const char* const names[4] = { "RECENT", "FITS NOW", "MORE", "LESS" };
        static const char* const abbrs[4] = { "RCNT", "FITS", "MORE", "LESS" };
        static const int colours[4] = { kViolet, kGreen, kWhite, kRed };
        std::vector<Enc> encs (16);
        for (int p = 1; p <= 4; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            e.name = names[p - 1]; e.abbr = abbrs[p - 1]; e.colour = colours[p - 1];
            e.channel = kFindChannel; e.number = kKnobCc + p - 1; e.def = 0;
            e.press = 1; e.pressChannel = kFindChannel; e.pressNumber = kPressCc + p - 1;
        }
        encs[12] = wheel(); encs[13] = key ("OPEN", "OPEN", kOpenKey);
        { Enc& e = encs[15]; e.name = "GLIDE"; e.abbr = "GLID"; e.colour = kGreen; e.channel = kSnapChannel; e.number = kGlideCc; e.def = 0; }
        add ("FIND", kFindChannel, encs);
    }
    for (int sl = 0; sl < sf0::kSlots; ++sl)        // (the rig's three insert slots: kSlots alone, in here, is the scene's count of addresses)
    {
        // an insert of the active lane: its eight places where the screen has them. What they are
        // depends on the lane and on the effect in the slot: SF-000 sends their names.
        std::vector<Enc> encs (16);
        for (int p = 1; p <= kCells; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            e.name = "INS" + String (sl + 1) + " " + String (p); e.abbr = "I" + String (sl + 1) + "." + String (p);
            e.colour = kWhite;
            e.channel = kFxChannel; e.number = kInsCc + sl * kCells + (p - 1); e.def = 0;
            e.press = 1; e.pressChannel = kFxChannel; e.pressNumber = kInsPressCc + sl * kCells + (p - 1);
        }
        encs[12] = wheel(); encs[13] = key ("OPEN", "OPEN", kOpenKey);
        add ("INS " + String (sl + 1), kFxChannel, encs);
    }
    {
        // (SPACE's four have the same names whichever reverb it is)
        static const char* const names[8] = { "DUB TIME", "DUB FDBK", "DUB TONE", "DUB DRV", "SPC SIZE", "SPC DCAY", "SPC DARK", "SPC DUCK" };
        static const char* const abbrs[8] = { "DTIM", "DFBK", "DTON", "DDRV", "SIZE", "DCAY", "DARK", "DUCK" };
        std::vector<Enc> encs (16);
        for (int p = 1; p <= 8; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            const int ctl = p <= 4 ? cx::kRet + (p - 1) : cx::space (eng.spaceModel(), p - 5);
            e.name = names[p - 1]; e.abbr = abbrs[p - 1];
            e.colour = p <= 4 ? kWhite : kLightBlue;
            e.channel = kSnapChannel; e.number = (p <= 4 ? ccx::kDub : ccx::kSpace) + (p - 1) % 4;
            e.def = to7 (eng.control (ctl), eng.control (ctl).def);
        }
        encs[12] = wheel(); encs[13] = key ("OPEN", "OPEN", kOpenKey);
        add ("RETURNS", kSnapChannel, encs);
    }
    {
        static const char* const names[3] = { "TAPE", "COMP", "LIMIT" };
        static const char* const abbrs[3] = { "TAPE", "COMP", "LIM" };
        std::vector<Enc> encs (16);
        for (int p = 1; p <= 3; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            e.name = names[p - 1]; e.abbr = abbrs[p - 1]; e.colour = kWhite;
            e.channel = kSnapChannel; e.number = ccx::kMaster + (p - 1);
            e.def = to7 (eng.control (cx::kMaster + p - 1), eng.control (cx::kMaster + p - 1).def);
        }
        encs[12] = wheel(); encs[13] = key ("OPEN", "OPEN", kOpenKey);
        add ("MASTER", kSnapChannel, encs);
    }
    {
        static const char* const names[5] = { "LENGTH", "HITS", "ROTATE", "ACCENTS", "SEED" };
        static const char* const abbrs[5] = { "LEN", "HITS", "ROT", "ACNT", "SEED" };
        std::vector<Enc> encs (16);
        for (int p = 1; p <= 5; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            e.name = names[p - 1]; e.abbr = abbrs[p - 1]; e.colour = p == 1 ? kWhite : kLightBlue;
            e.channel = kFxChannel; e.number = p == 1 ? kLenCc : kGenCc + (p - 2); e.def = p == 1 ? 15 : 0;
        }
        encs[12] = wheel(); encs[13] = key ("OPEN", "OPEN", kOpenKey);
        add ("SEQ", kFxChannel, encs);
    }
    for (int c = 0; c < kCfgPages; ++c)
    {
        // CONFIG's controls, twelve to a page, in the order its panel has them: SF-000 sends their names
        std::vector<Enc> encs (16);
        for (int p = 1; p <= kCfgPer; ++p)
        {
            Enc& e = encs[(size_t) (p - 1)];
            const int n = c * kCfgPer + p;
            e.name = "CFG " + String (n); e.abbr = "C" + String (n); e.colour = kWhite;
            e.channel = kFxChannel; e.number = cfgCc (n - 1); e.def = 0;
        }
        encs[12] = wheel(); encs[13] = key ("SHUT", "SHUT", kOpenKey);
        encs[14] = key ("PAGE -", "PG -", kCfgPrevKey); encs[15] = key ("PAGE +", "PG +", kCfgNextKey);
        add ("CFG " + String (c + 1), kFxChannel, encs);
    }
    add ("-", 1, {});

    String src = String::fromUTF8 (BinaryData::rings_lua, BinaryData::rings_luaSize).replace ("\r\n", "\n");
    src = src.replace ("@MAP@", map).replace ("@GREEN@", String (kGreen)).replace ("@FOCUS@", focus.joinIntoString (", "))
             .replace ("@FOCUS_CH@", String (kSnapChannel - 1)).replace ("@FOCUS_CC@", String (kFocusCc));
    // the icon: a four by four grid of dots
    std::vector<std::string> icon;
    for (int r = 0; r < 16; ++r)
    {
        int row = 0;
        for (int c = 0; c < 16; ++c) if (r % 4 == 1 && c % 4 == 1) row |= 3 << (14 - c);
        if (r % 4 == 2) for (int c = 0; c < 16; ++c) if (c % 4 == 1) row |= 3 << (14 - c);
        icon.push_back (Json::num (row >> 8)); icon.push_back (Json::num (row & 0xff));
    }
    const std::string code = Json::obj ({ { "code", Json::str (minified (src)) }, { "fullScript", Json::str (src.replace ("\n", "\r\n")) },
                                          { "scriptName", Json::str ("sf_glide_rings") } });
    const std::string scene = Json::obj ({
        { "title", Json::str ("SF-FX") }, { "icon", Json::arr (icon) }, { "selectedPreset", "0" }, { "color", Json::num (kViolet) },
        { "transmitMode", "0" }, { "pcOnEntry", "0" }, { "bankOnEntry", "65535" }, { "recentPage", "0" }, { "recentPreset", "0" },
        { "transmitOnSceneEntry", "0" }, { "transmitOnPageSwitch", "0" }, { "transmitOnPresetLoad", "0" }, { "smartTransmit", "15" },
        { "outputOnEntry", "0" }, { "holdMode", "0" }, { "acceleration", "3" }, { "code", code }, { "pages", Json::arr (pages) } });
    return String (scene);
}
} // namespace e16

// ===================================================================================== the ports
static bool isE16 (const String& name) { return name.toUpperCase().contains ("E16"); }

// The E16 has several USB ports and sends everything on each: one of them only, port 1.
static int pickE16 (const juce::Array<juce::MidiDeviceInfo>& devs)
{
    int first = -1, plain = -1;
    for (int i = 0; i < devs.size(); ++i)
    {
        const String s = devs[i].name.toUpperCase().trim();
        if (! s.contains ("E16")) continue;
        if (s.endsWith (" 1") || s.contains ("PORT 1") || s.endsWith ("PORT1")) return i;
        if (first < 0) first = i;
        if (plain < 0 && ! s.contains ("PORT")) plain = i;
    }
    return plain >= 0 ? plain : first;
}

Midi::Midi (Engine& e, Session& s, bool devices) : juce::Thread ("SF-000 clock"), engine (e), ses (s), useDevices (devices)
{
    for (auto& v : latest) v = -1;
    for (auto& v : sent) v = -1;
    for (auto& v : heardAt) v = 0;
    for (auto& v : presses) v = 0;
    for (auto& v : libLatest) v = -1;
    for (auto& v : libSent) v = -1;
    for (auto& v : libPresses) v = 0;
    for (auto& v : libHeardAt) v = 0;
    for (auto& v : libTurn) v = 0;
    for (auto& v : exLatest) v = -1;
    for (auto& v : exBy) v = 2;
    for (auto& v : recallHeard) v = -1;
    for (auto& v : recallIn) v = 0;
    for (auto& v : heldSince) v = 0;
    for (auto& v : heldFired) v = false;
    for (auto& v : bSent) v = -1;
    for (auto& v : bHeardAt) v = 0;
    for (auto& v : genIn) v = -1;
    for (auto& v : cfgIn) v = -1;
    laneNow = ses.activeLane;
    ringLast.fill (0); ringSentAt.fill (0.0);
    if (useDevices)
    {
        load();
        scan();
        startThread (juce::Thread::Priority::highest);
        startTimerHz (40);
    }
}

Midi::~Midi()
{
    cancelPendingUpdate();
    stopTimer();
    engine.setClockOut (false, leadMs);
    if (useDevices) stopThread (500);
    e16In = nullptr;
    for (auto& in : inputs) in->stop();
    inputs.clear();
    const juce::ScopedLock g (outLock);
    clockOuts.clear();
    e16Out = nullptr;
}

juce::File Midi::file() { return Session::stateFile().getSiblingFile ("midi.json"); }

void Midi::load()
{
    const juce::var v = juce::JSON::parse (file());
    if (! v.isObject()) return;
    clockWanted = (bool) v.getProperty ("clock", true);
    leadMs = juce::jlimit (0.0f, 60.0f, (float) (double) v.getProperty ("lead", 0.0));
    followWanted = (bool) v.getProperty ("follow", false);
    beatWanted = (bool) v.getProperty ("on_beat", false);
    trimMs = juce::jlimit (-40.0f, 40.0f, (float) (double) v.getProperty ("trim", 0.0));
    pcCh = juce::jlimit (0, 16, (int) v.getProperty ("e16_pc_channel", e16::kPcChannel));
    sceneSlots[0] = juce::jlimit (1, 16, (int) v.getProperty ("e16_scene", 1));
    sceneSlots[1] = juce::jlimit (1, 16, (int) v.getProperty ("e16_scene_fx", 2));
    pageFirst = juce::jlimit (0, 1, (int) v.getProperty ("e16_page_first", 0));
    sceneAlways = (bool) v.getProperty ("e16_scene_always", false);         // (not "e16_bank_always", which was true in every file written before the scene was left out of a turn within it)
}

void Midi::save() const
{
    if (! useDevices) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("clock", clockWanted);
    o->setProperty ("lead", leadMs);
    o->setProperty ("follow", followWanted);
    o->setProperty ("on_beat", beatWanted);
    o->setProperty ("trim", trimMs);
    o->setProperty ("e16_pc_channel", pcCh);
    o->setProperty ("e16_scene", sceneSlots[0]);
    o->setProperty ("e16_scene_fx", sceneSlots[1]);
    o->setProperty ("e16_scene_always", sceneAlways);
    o->setProperty ("e16_page_first", pageFirst);
    file().getParentDirectory().createDirectory();
    file().replaceWithText (juce::JSON::toString (juce::var (o)));
}

// The clock is followed or sent, never both: a follower that also sent would be answered by itself.
void Midi::apply()
{
    engine.setFollow (followWanted, beatWanted, trimMs);
    engine.setClockOut (clockWanted && ! followWanted && ! clockOuts.empty(), leadMs);
}

void Midi::setSendsClock (bool on)      { clockWanted = on; apply(); save(); }
void Midi::setLead (float ms)           { leadMs = juce::jlimit (0.0f, 60.0f, ms); apply(); save(); }
void Midi::setFollowsClock (bool on)    { followWanted = on; apply(); save(); }
void Midi::setOnBeat (bool on)          { beatWanted = on; apply(); save(); }
void Midi::setTrim (float ms)           { trimMs = juce::jlimit (-40.0f, 40.0f, ms); apply(); save(); }
void Midi::setPcChannel (int channel)   { pcCh = juce::jlimit (0, 16, channel); save(); }
void Midi::setSceneSlot (int which, int slot) { sceneSlots[(size_t) juce::jlimit (0, 1, which)] = juce::jlimit (1, 16, slot); e16At = {}; save(); }

String Midi::clockSource() const
{
    const juce::SpinLock::ScopedLockType g (clockSrcLock);
    return clockSrc != nullptr && juce::Time::getMillisecondCounterHiRes() - clockSrcAt < 1000.0 ? clockSrcName : String();
}

void Midi::clockFrom (const void* source, const String& name, juce::uint8 byte, double arrivedMs)
{
    {
        const juce::SpinLock::ScopedLockType g (clockSrcLock);
        if (source != clockSrc)
        {
            if (clockSrc != nullptr && arrivedMs - clockSrcAt < 1000.0) return;       // another input is the clock
            clockSrc = source; clockSrcName = name;
        }
        clockSrcAt = arrivedMs;
    }
    engine.clockIn (byte, arrivedMs);
}

juce::StringArray Midi::inputNames() const { juce::StringArray n; for (auto& i : inputs) n.add (i->getName()); return n; }
juce::StringArray Midi::clockNames() const { juce::StringArray n; for (auto& o : clockOuts) n.add (o->getName()); return n; }
String Midi::e16Name() const { return e16Out != nullptr ? e16Out->getName() : String(); }

void Midi::scan()
{
    // inputs: every one there is, and of the E16's only its first port
    const auto ins = juce::MidiInput::getAvailableDevices();
    const int e16Idx = pickE16 (ins);
    for (size_t i = inputs.size(); i-- > 0;)
    {
        bool there = false;
        for (auto& d : ins) there = there || d.identifier == inputs[i]->getIdentifier();
        if (! there)
        {
            if (e16In.load() == inputs[i].get()) e16In = nullptr;
            { const juce::SpinLock::ScopedLockType g (clockSrcLock); if (clockSrc == inputs[i].get()) clockSrc = nullptr; }
            inputs[i]->stop();
            inputs.erase (inputs.begin() + (long) i);
        }
    }
    for (int i = 0; i < ins.size(); ++i)
    {
        if (isE16 (ins[i].name) && i != e16Idx) continue;
        bool open = false;
        for (auto& in : inputs) open = open || in->getIdentifier() == ins[i].identifier;
        if (open) continue;
        if (auto in = juce::MidiInput::openDevice (ins[i].identifier, this))
        {
            if (i == e16Idx) e16In = in.get();
            in->start();
            inputs.push_back (std::move (in));
        }
    }

    // outputs: the E16's first port for its rings, every other one for the clock
    const auto outs = juce::MidiOutput::getAvailableDevices();
    const int e16O = pickE16 (outs);
    const String wantId = e16O >= 0 ? outs[e16O].identifier : String();
    if (wantId != e16OutId)
    {
        e16Out = wantId.isNotEmpty() ? juce::MidiOutput::openDevice (wantId) : nullptr;
        e16OutId = e16Out != nullptr ? wantId : String();
        for (auto& v : sent) v = -1;            // a fresh E16 is told every value
        glideSent = -1;
        ringOn.fill (false);
        e16At = {};                             // (and which page it is on is not known until it says, or is sent to one)
        syncWanted = true;
        settling = false; againLeft = 0; e16Speaks = false; strayed = {};
        for (auto& v : bSent) v = -1;
        wheelAddr = -1; wheelHeard = -1; wheelTold = -1;
        namesSaid = {};
    }
    {
        const juce::ScopedLock g (outLock);
        for (size_t i = clockOuts.size(); i-- > 0;)
        {
            bool there = false;
            for (auto& d : outs) there = there || d.identifier == clockOuts[i]->getIdentifier();
            if (! there) clockOuts.erase (clockOuts.begin() + (long) i);
        }
        for (auto& d : outs)
        {
            if (isE16 (d.name)) continue;
            bool open = false;
            for (auto& o : clockOuts) open = open || o->getIdentifier() == d.identifier;
            if (! open) if (auto o = juce::MidiOutput::openDevice (d.identifier)) clockOuts.push_back (std::move (o));
        }
    }
    apply();
}

// ==================================================================================== messages
void Midi::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    const bool fromE16 = source != nullptr && source == e16In.load();
    if (m.getRawDataSize() == 1 && ! fromE16)
    {
        const auto byte = m.getRawData()[0];
        if (byte == 0xF8 || byte == 0xFA || byte == 0xFB || byte == 0xFC)
            clockFrom (source, source != nullptr ? source->getName() : String(), byte, juce::Time::getMillisecondCounterHiRes());
        return;
    }
    incoming (m, fromE16);
}

// Knob 13 of a lane's page (SEND A) or of the snapshot page: the one that is the pointer's wheel while the wheel is in use.
static bool isKnob13 (int slot)
{
    return slot >= kLanes * e16::kPerLane ? slot - kLanes * e16::kPerLane == 12 : slot % e16::kPerLane == 12;
}

// A turn of the wheel's knob. How far: from the number it last said, or from the number it was
// told to stand at since, whichever it is nearer to (the E16 may or may not have taken that
// number: a knob still under a hand does not always). A jump that is neither is no turn: the
// knob's number is taken as it is.
void Midi::wheelIn (int address, int val, juce::uint32 nowMs)
{
    // How far it was turned: from the number it last said, or from the one it has been told
    // since if it took that (a knob in a hand does not always). Whichever makes the smaller
    // turn; a knob only speaks when it has moved, so never the one it is standing on. A leap is
    // the knob having been somewhere else, not a turn.
    int d = 0;
    if (wheelAddr.exchange (address) == address)
    {
        const int h = wheelHeard.load(), t = wheelTold.load();
        int best = 1000;
        if (h >= 0 && val != h) best = val - h;
        if (t >= 0 && val != t && std::abs (val - t) < std::abs (best)) best = val - t;
        if (std::abs (best) <= 16) d = best;
    }
    wheelHeard = val; wheelTold = -1; wheelHeardAt = nowMs;
    if (d != 0) wheelTurn.fetch_add (d);
}

// Knobs 13 and 14 where a press already recalls (the snapshot page, the library's map): held for
// half a second they are the wheel and OPEN, and the recall is for a press let go before that.
void Midi::held (int which, bool down, juce::uint32 nowMs, std::atomic<int>& shortPress, bool wheel)
{
    const auto i = (size_t) which;
    if (down) { heldFired[i] = false; heldSince[i] = nowMs == 0 ? 1u : nowMs; return; }
    const bool wasDown = heldSince[i].exchange (0) != 0;
    if (wasDown && ! heldFired[i].exchange (false)) { if (wheel) otherTurned = true; shortPress.fetch_add (1); }
}

// An address that is on no page of the first scene (an insert's place, a return's knob, the
// master's, a lane's keys), from a sequencer or from the second scene's pages.
void Midi::takeExtra (int extra, int val, bool fromE16)
{
    // A key (MUTE, SOLO, a lane's sequencer muted, a hold like THROW or FREEZE) acts at once,
    // from this thread: on the next block of sound, not when the screen's thread next comes
    // round (up to 25 ms, more if it is busy). Every other address waits for that thread (an
    // insert's place is whatever effect is in it when it comes), but in the order they came
    // and with nothing dropped; it also keeps the session's own record of the keys.
    const int ctl = ccx::controlFor (extra, engine.spaceModel());
    const auto& c = engine.control (ctl);
    const bool key = c.used && (c.kind == Kind::Toggle || c.kind == Kind::Hold);
    if (key) engine.set (ctl, val >= 64 ? 1.0f : 0.0f);
    const juce::SpinLock::ScopedLockType g (exLock);
    if (exN < kExQ) exQ[(size_t) exN++] = { (juce::uint16) extra, (juce::uint8) val, (juce::uint8) (fromE16 ? 1 : 2), key };
    else { exBy[(size_t) extra] = fromE16 ? 1 : 2; exLatest[(size_t) extra] = val; }
}

void Midi::incoming (const juce::MidiMessage& m, bool fromE16)
{
    if (m.isNoteOnOrOff())
    {
        if (! fromE16) engine.noteIn (m, juce::Time::getMillisecondCounterHiRes());
        return;
    }
    if (! m.isController()) return;
    const int ch = m.getChannel(), num = m.getControllerNumber(), val = m.getControllerValue();
    const auto nowMs = juce::Time::getMillisecondCounter();
    // The pointer's wheel is in use: knob 13 of whatever page is up moves the pointer and nothing
    // else. Anything else turned or pressed on the E16 says the hand has moved on.
    const bool wheel = fromE16 && wheelMode.load();
    auto other = [&] { if (wheel) otherTurned = true; };

    if (ch == e16::kMapChannel || ch == e16::kFindChannel)
    {
        const int base = ch == e16::kMapChannel ? 0 : 16;
        if (num >= e16::kKnobCc && num < e16::kKnobCc + 16)
        {
            const auto k = (size_t) (base + num - e16::kKnobCc);
            if (fromE16) pageHeard = e16::kFocusB + (base == 0 ? (int) e16::pgMap : (int) e16::pgFind);
            if (wheel && k == 12)
            {
                wheelIn (ch * 128 + num, val, nowMs);
                libSent[k] = val; libHeardAt[k] = nowMs;
                return;
            }
            other();
            // how far it was turned, from where the knob was (its number is the E16's own business:
            // SF-000 moves it to show a place, and that must not count as a turn)
            if (const int was = libSent[k].exchange (val); was >= 0) libTurn[k].fetch_add (val - was);
            libLatest[k] = val; libHeardAt[k] = nowMs;
        }
        else if (num >= e16::kPressCc && num < e16::kPressCc + 16)
        {
            const int k = base + num - e16::kPressCc;
            if (fromE16 && (k == 12 || k == 13)) held (2 + (k - 12), val >= 64, nowMs, libPresses[(size_t) k], wheel);
            else if (val >= 64) { other(); libPresses[(size_t) k].fetch_add (1); }
        }
        return;
    }
    if (ch == e16::kSnapChannel)
    {
        if (num == e16::kFocusCc)
        {
            if ((val >= 1 && val <= e16::kSnapPage) || (val > e16::kFocusB && val <= e16::kFocusB + 12)) pageUp = val;
            return;
        }
        if (num == e16::kWheelCc)
        {
            // the wheel's own address (a page where knob 13 is nothing else): always the pointer's
            wheelIn (ch * 128 + num, val, nowMs);
            if (fromE16) bMark (12, val, nowMs);
            return;
        }
        if (num == e16::kWheelKey) { if (val >= 64) wheelKeys.fetch_add (1); return; }
        if (num == e16::kOpenKey)  { if (val >= 64) openKeys.fetch_add (1); return; }
        if (num == e16::kCfgPrevKey || num == e16::kCfgNextKey) { if (val >= 64) cfgStep.fetch_add (num == e16::kCfgNextKey ? 1 : -1); return; }
        if (num == e16::kGlideCc)
        {
            other();
            // GLIDE is taken up at once, from this thread, as a lane's knob is: a knob sent just
            // after it travels by it. (It waited for the message thread, and the knob did not.)
            engine.set (cx::kGlide, e16::from7 (engine.control (cx::kGlide), val));
            glideFromE16 = fromE16; glideIn = val; ses.moved (cx::kGlide, fromE16 ? 1 : 2);
            return;
        }
        if (num >= e16::kPressCc && num < e16::kPressCc + 16)
        {
            const int k = num - e16::kPressCc;
            if (fromE16) pageHeard = e16::kSnapPage;
            if (fromE16 && (k == 12 || k == 13)) held (k - 12, val >= 64, nowMs, presses[(size_t) k], wheel);
            else if (val >= 64) { other(); presses[(size_t) k].fetch_add (1); }
            return;
        }
        if (num >= ccx::kRecall && num < ccx::kRecall + 16)
        {
            // RECALL: which of the snapshots a snapshot knob holds. Only a new value acts.
            const auto k = (size_t) (num - ccx::kRecall);
            if (recallHeard[k].exchange (val) != val && val > 0) { recallIn[k] = val; if (useDevices) triggerAsyncUpdate(); }
            return;
        }
    }
    if (ch == e16::kFxChannel)
    {
        // the second scene's pages: what they hold belongs to whatever is in front
        other();
        const int lane = laneNow.load();
        if (num >= e16::kInsCc && num < e16::kInsCc + kSlots * kCells)
        {
            const int j = num - e16::kInsCc;
            if (fromE16) { bMark (j % kCells, val, nowMs); pageHeard = e16::kFocusB + e16::pgIns + j / kCells; }
            takeExtra (lane * ccx::kPerLane + j, val, fromE16);
        }
        else if (num >= e16::kInsPressCc && num < e16::kInsPressCc + kSlots * kCells)
        {
            // a place pressed: a key among them is held (THROW) or switched (FREEZE); a knob's press is nothing
            const int extra = lane * ccx::kPerLane + (num - e16::kInsPressCc);
            const int ctl = ccx::controlFor (extra, engine.spaceModel());
            const auto& c = engine.control (ctl);
            if (c.used && c.kind == Kind::Hold) takeExtra (extra, val >= 64 ? 127 : 0, fromE16);
            else if (c.used && c.kind == Kind::Toggle && val >= 64) takeExtra (extra, engine.target (ctl) > 0.5f ? 0 : 127, fromE16);
        }
        else if (num == e16::kLenCc) { lenIn = val; if (fromE16) { bMark (0, val, nowMs); pageHeard = e16::kFocusB + e16::pgSeq; } }
        else if (num >= e16::kGenCc && num < e16::kGenCc + 4) { genIn[(size_t) (num - e16::kGenCc)] = val; if (fromE16) { bMark (1 + num - e16::kGenCc, val, nowMs); pageHeard = e16::kFocusB + e16::pgSeq; } }
        else if (const int n = e16::cfgAt (num); n >= 0)
        {
            cfgIn[(size_t) n] = val;
            if (fromE16) { bMark (n % e16::kCfgPer, val, nowMs); pageHeard = e16::kFocusB + e16::pgCfg + n / e16::kCfgPer; }
        }
        return;
    }
    if (const int extra = ccx::extraFor (ch, num); extra >= 0)
    {
        other();
        if (fromE16 && ch == e16::kSnapChannel)        // (the returns' page, the master's)
        {
            bMark (num >= ccx::kMaster ? num - ccx::kMaster : num - ccx::kDub, val, nowMs);
            pageHeard = e16::kFocusB + (num >= ccx::kMaster ? (int) e16::pgMaster : (int) e16::pgRet);
        }
        takeExtra (extra, val, fromE16);
        return;
    }
    const int slot = e16::slotFor (ch, num);
    if (slot < 0) return;
    if (fromE16) pageHeard = slot >= kLanes * e16::kPerLane ? (int) e16::kSnapPage : slot / e16::kPerLane + 1;
    if (wheel && isKnob13 (slot))
    {
        wheelIn (ch * 128 + num, val, nowMs);
        sent[(size_t) slot] = val; heardAt[(size_t) slot] = nowMs;
        return;
    }
    other();
    const int ctl = e16::controlFor (slot);
    if (fromE16) { sent[(size_t) slot] = val; heardAt[(size_t) slot] = nowMs; }
    ses.moved (ctl, fromE16 ? 1 : 2);           // (who moved it: the 3D stage's eye goes to a hand on the E16, not to a sequencer)
    // A lane's knob is taken up at once, from this thread (and travels over the GLIDE time, if one
    // is set). The session's record of it (the base scene under the snapshot knobs) follows on the
    // message thread, as does a snapshot knob's whole move.
    if (ctl < cx::kSnap) engine.set (ctl, e16::from7 (engine.control (ctl), val), engine.control (ctl).glides ? glideBarsFor (engine.target (cx::kGlide)) * 4.0 : 0.0);
    latest[(size_t) slot] = val;
}

void Midi::sendE16 (const juce::MidiMessage& m)
{
    if (toE16) toE16 (m);
    else if (e16Out != nullptr) e16Out->sendMessageNow (m);
}

void Midi::timerCallback()
{
    if (--scanIn <= 0) { scanIn = 80; scan(); }         // every two seconds: what was plugged in or pulled
    service();
}

void Midi::service()
{
    // what came in since last time
    for (int slot = 0; slot < e16::kSlots; ++slot)
    {
        const int v = latest[(size_t) slot].exchange (-1);
        if (v < 0) continue;
        const int ctl = e16::controlFor (slot);
        if (ctl >= cx::kSnap) ses.turnSnapKnob (ctl - cx::kSnap, (float) v / 127.0f);
        else ses.userSet (ctl, e16::from7 (engine.control (ctl), v), true);
    }
    for (int k = 0; k < 16; ++k)
        if (presses[(size_t) k].exchange (0) > 0) ses.pressSnapKnob (k);
    const auto now = juce::Time::getMillisecondCounter();
    if (const int said = pageUp.exchange (0); said > 0) { pageHeard = 0; pageCame (said, now); }
    else if (const int on = pageHeard.exchange (0); on > 0 && now - turnedAt > 300u && ! (settling && sceneTurn))
    {
        // A knob turned on the E16 is on a page: if that is not the page it was taken to be on
        // (it has not said, or its script does not say), it is on that one. Nothing comes to the
        // front for it, and nothing is told to the knob under the hand.
        const Where at = on > e16::kFocusB ? Where { 1, on - e16::kFocusB } : Where { 0, on };
        if (at != e16At) arrived (at, true);
    }
    takeGlide();
    takeRecalls();
    {
        // what came on the other addresses, in the order it came. A knob's last place is the one
        // that counts; a key's every press and letting go is kept (they are already in the sound:
        // only the last is put to the engine again, so that a recall made meanwhile is answered).
        std::array<ExEvent, kExQ> batch;
        int nb = 0;
        {
            const juce::SpinLock::ScopedLockType g (exLock);
            nb = exN; exN = 0;
            std::copy (exQ.begin(), exQ.begin() + nb, batch.begin());
        }
        std::array<int, ccx::kExtras> lastAt; lastAt.fill (-1);
        for (int i = 0; i < nb; ++i) lastAt[batch[(size_t) i].extra] = i;
        for (int i = 0; i < nb; ++i)
        {
            const auto& e = batch[(size_t) i];
            const int ctl = ccx::controlFor (e.extra, engine.spaceModel());
            const auto& c = engine.control (ctl);
            if (! c.used) continue;                                 // an empty place
            const bool key = c.kind == Kind::Toggle || c.kind == Kind::Hold, last = lastAt[e.extra] == i;
            if (! key && ! last) continue;
            ses.moved (ctl, e.by);
            ses.userSet (ctl, key ? (e.val >= 64 ? 1.0f : 0.0f) : e16::from7 (c, e.val), key && e.done && ! last);
        }
    }
    for (int extra = 0; extra < ccx::kExtras; ++extra)
    {
        const int v = exLatest[(size_t) extra].exchange (-1);
        if (v < 0) continue;
        const int ctl = ccx::controlFor (extra, engine.spaceModel());
        const auto& c = engine.control (ctl);
        if (! c.used) continue;                                 // an empty place
        ses.moved (ctl, exBy[(size_t) extra].load());
        ses.userSet (ctl, c.kind == Kind::Toggle || c.kind == Kind::Hold ? (v >= 64 ? 1.0f : 0.0f) : e16::from7 (c, v));
    }
    // the second scene's pages: the sequencer strip's knobs, CONFIG's controls
    if (const int v = lenIn.exchange (-1); v >= 0)
    {
        Pattern pat = ses.pattern (ses.activeLane);
        if (const int n = juce::jlimit (1, kMaxSteps, v + 1); n != pat.length) { pat.length = n; ses.setPattern (ses.activeLane, pat); if (ses.onChange) ses.onChange(); }
    }
    for (int j = 0; j < 4; ++j)
        if (const int v = genIn[(size_t) j].exchange (-1); v >= 0)
        {
            const int ctl = cx::gen (ses.activeLane, j);
            ses.moved (ctl, 1);
            ses.userSet (ctl, e16::from7 (engine.control (ctl), v));
        }
    if (ses.cfgOpen != cfgWasOpen || ses.cfg.kind != cfgWas.kind || ses.cfg.lane != cfgWas.lane || ses.cfg.slot != cfgWas.slot)
    {
        // CONFIG opened, shut or moved to another panel: its first page, and its controls looked at afresh
        cfgWas = ses.cfg; cfgWasOpen = ses.cfgOpen; cfgPage = 0; cfgCacheAge = 0;
        for (auto& v : cfgIn) v = -1;
    }
    for (int k = 0; k < e16::kCfgPer * e16::kCfgPages; ++k)
        if (const int v = cfgIn[(size_t) k].exchange (-1); v >= 0) { ses.setConfig (k, v); cfgCacheAge = 0; }
    if (--cfgCacheAge <= 0) { cfgCacheAge = 6; cfgCache = ses.configList(); }
    serviceWheel (now);
    if ((openKeys.exchange (0) & 1) != 0)
    {
        if (wheelMode.load()) wheelChoose();        // (what the pointer is on comes to the front first: OPEN is for that)
        openPressed();
    }
    if (const int st = cfgStep.exchange (0); st != 0 && ses.cfgOpen) cfgPage = juce::jlimit (0, cfgPagesNow() - 1, cfgPage + (st > 0 ? 1 : -1));
    laneNow = ses.activeLane;

    // A page that has just come up is told its values again, a moment after it has settled.
    if (pageWait > 0 && --pageWait == 0)
    {
        const int first = (pageNow - 1) * e16::kPerLane, count = pageNow == e16::kSnapPage ? 16 : e16::kPerLane;
        for (int k = 0; k < count && first + k < e16::kSlots; ++k) sent[(size_t) (first + k)] = -1;
        glideSent = -1;
    }
    if (bWait > 0 && --bWait == 0)
    {
        for (auto& v : bSent) v = -1;
        if (! bHeard) for (auto& v : bHeardAt) v = 0;          // (found by a knob's turn: that knob is still under the hand)
        bHeard = false; wheelTold = -1; wheelTries = 0;
    }
    serviceLibrary();

    // Where the E16 should be for what is in front. It is sent there when that changes here (a
    // module brought to the front on the screen, by a key, by the wheel; CONFIG or the library
    // opened), and never because of a page it turned to itself: that is where it already is.
    const Where want = whereNow();
    const bool turn = whereKnown && want != whereWas && want != e16At;
    whereWas = want; whereKnown = true;

    if (! toE16 && e16Out == nullptr) return;
    // (an E16 newly here is turned to what is in front, once: from then on SF-000 knows its page)
    if (pcCh > 0 && (turn || (syncWanted && e16At.scene < 0)))
    {
        const bool first = ! turn;
        turnTo (want, now);
        if (first) { pageWait = 0; bWait = 0; }                 // (it is being told every value as it is)
    }
    syncWanted = false;
    settle (now);
    // The E16's rings follow the rig: a value that changed here (a recall, the mouse, a snapshot
    // knob's push) is sent to its address. A knob the hand is on is left alone for a moment.
    // No more than a couple of dozen at a time (a fresh E16 is told some two hundred, and a burst
    // of that size can be more than it takes in), the page that is up first.
    int room = 24;
    const bool wheelOn = wheelMode.load();
    const int from = pageNow > 0 ? (pageNow - 1) * e16::kPerLane : 0;
    for (int k = 0; k < e16::kSlots && room > 0; ++k)
    {
        const int slot = (from + k) % e16::kSlots;
        if (wheelOn && isKnob13 (slot)) continue;               // (the pointer's wheel for now: its number is the pointer's place)
        const int ctl = e16::controlFor (slot);
        const int want = e16::to7 (engine.control (ctl), engine.target (ctl));
        if (want == sent[(size_t) slot].load() || now - heardAt[(size_t) slot].load() < 150u) continue;
        int ch, num;
        e16::addressOf (slot, ch, num);
        sendE16 (juce::MidiMessage::controllerEvent (ch, num, want));
        sent[(size_t) slot] = want;
        --room;
    }
    if (const int want = e16::to7 (engine.control (cx::kGlide), engine.target (cx::kGlide)); want != glideSent && now - glideHeardAt >= 150u)
    {
        sendE16 (juce::MidiMessage::controllerEvent (e16::kSnapChannel, e16::kGlideCc, want));
        glideSent = want;
    }
    placeWheel (now);
    feedPage (now);
    sayNames();
    rings (juce::Time::getMillisecondCounterHiRes());
}

// ------------------------------------------------------------------ what is in front, on the E16
Midi::Where Midi::whereNow() const
{
    using S = Session;
    if (ses.showing == 1) return { 1, e16At.scene == 1 && e16At.page == e16::pgFind ? (int) e16::pgFind : (int) e16::pgMap };
    if (ses.showing == 2) return { 0, e16::kSnapPage };
    if (ses.cfgOpen) return { 1, e16::pgCfg + juce::jlimit (0, e16::kCfgPages - 1, cfgPage) };
    switch (ses.front.kind)
    {
        case S::fLane:    return { 0, ses.activeLane + 1 };
        case S::fSnap:    return { 0, e16::kSnapPage };
        case S::fInsert:  return { 1, e16::pgIns + ses.front.slot };
        case S::fReturns: return { 1, e16::pgRet };
        case S::fMaster:  return { 1, e16::pgMaster };
        default:          return { 1, e16::pgSeq };
    }
}

// The E16 is on this page. A page of the first scene is told its values again (each has its own
// address, so they are right already: this is for whatever the E16 made of them meanwhile); one
// of the second is told them for the first time, its addresses being whatever is in front. Not
// when the page was found by a knob's turn: the first scene's are right, and the knob is in a hand.
void Midi::arrived (Where w, bool heard)
{
    e16At = w;
    if (w.scene == 0) { pageNow = w.page; if (! heard) pageWait = 2; }
    else { bWait = 2; bHeard = heard; }
    namesSaid = {};
}

// The E16 says which page is up. What is in front follows it, and nothing is sent back for that.
void Midi::pageCame (int said, juce::uint32 now)
{
    using S = Session;
    const Where at = said > e16::kFocusB ? Where { 1, said - e16::kFocusB } : Where { 0, said };
    e16Speaks = true;
    if (settling && at == asked)
    {
        // Its answer: it is there. Never a hand's turn, whatever has come to the front since
        // (something else may have, and the E16 is about to be sent on to it).
        settling = false; againLeft = 0; pcNote = {};
        arrived (at);
        return;
    }
    if (settling && (juce::int32) (now - settleUntil) < 0)
    {
        // It was just sent somewhere and says it is somewhere else. Nothing follows it there: it
        // is its answer to being sent (or to having been sent somewhere a moment before, with
        // this turn still on its way), not a hand's turn.
        if (at.scene == asked.scene && pageTries < 3)
        {
            // The scene is right and the page is not. A scene that loads puts its first page up,
            // and a Program Change that comes with the Bank Select can be lost in the loading (or
            // be acted on before it): now that the scene's script is talking, the page again, by
            // itself (settle() sends it: at once the first time, a moment later after that, in
            // case it is a knob still held down that is in the way).
            againLeft = 1; againAt = now + (pageTries == 0 ? 0u : 200u);
            ++pageTries;
            turnedAt = now; settleUntil = now + 700; answerBy = now + 900;
        }
        strayed = at;                               // (settle() says so in SETUP if it never comes round)
        return;
    }
    settling = false;                               // (a hand has turned it since)
    // (its answer to being sent somewhere, or its script starting again on the page it was on: it
    // is where what is in front has it, and nothing comes to the front a second time for that)
    const bool there = at == whereNow();
    arrived (at);
    if (there) return;
    if (at.scene == 0)
    {
        if (at.page <= kLanes) ses.bringLane (at.page - 1, 2); else ses.bring ({ S::fSnap, 0 }, 2);
        if (ses.showing != 0) ses.wantShowing = 0;
    }
    else if (at.page == e16::pgMap || at.page == e16::pgFind) { if (ses.showing != 1) ses.wantShowing = 1; }
    else
    {
        if (ses.showing != 0) ses.wantShowing = 0;
        if (at.page >= e16::pgIns && at.page < e16::pgIns + kSlots) ses.bring ({ S::fInsert, at.page - e16::pgIns }, 2);
        else if (at.page == e16::pgRet)    ses.bring ({ S::fReturns, 0 }, 2);
        else if (at.page == e16::pgMaster) ses.bring ({ S::fMaster, 0 }, 2);
        else if (at.page == e16::pgSeq)    ses.bring ({ S::fSeq, 0 }, 2);
        else if (at.page >= e16::pgCfg && at.page < e16::pgCfg + e16::kCfgPages)
        {
            if (! ses.cfgOpen) ses.configOfFront (2);
            cfgWas = ses.cfg; cfgWasOpen = ses.cfgOpen; cfgCacheAge = 0;
            cfgPage = at.page - e16::pgCfg;
        }
    }
}

juce::String Midi::pageName (Where w)
{
    static const char* const b[12] = { "MAP", "FIND", "INS 1", "INS 2", "INS 3", "RETURNS", "MASTER", "SEQ", "CFG 1", "CFG 2", "CFG 3", "PAGE 12" };
    if (w.page < 1 || w.page > 12) return "NOWHERE";
    return w.scene == 1 ? "SF-FX " + String (b[w.page - 1]) : "SF-000 PAGE " + String (w.page);
}

// Within a scene a Program Change by itself says the page (the E16's PC Mode must be Page: by
// itself a Program Change is a page or a scene according to that). To another scene, Bank Select
// first (by the slot the scene is in on the E16: controller 32, the one Ableton calls Sub, which
// is what the E16's manual says picks the scene; controller 0 goes with it as nought) and the
// Program Change with it, as the manual has it.
//
// The scene is not said for a page of the scene it is already in: on Wiley's E16 every turn sent
// with a Bank Select ended on the scene's first page (KICK's), whatever page was asked for. A
// scene that is selected is loaded, the one it is in included, and comes up on its first page;
// the Program Change sent with it does not survive that. So after a change of scene the page is
// said again by itself: when the scene's script says which page is up and it is not the one
// (pageCame), and twice unasked in case the script says nothing (settle).
// "e16_scene_always" true in midi.json says the scene with every page, as before.
void Midi::turnTo (Where w, juce::uint32 now)
{
    const bool scene = sceneAlways || w.scene != e16At.scene;
    if (scene)
    {
        sendE16 (juce::MidiMessage::controllerEvent (pcCh, 0, 0));
        sendE16 (juce::MidiMessage::controllerEvent (pcCh, 32, sceneSlots[(size_t) juce::jlimit (0, 1, w.scene)] - 1));
    }
    sendE16 (juce::MidiMessage::programChange (pcCh, w.page - 1 + pageFirst));
    turnedAt = now; asked = w;
    settling = true; sceneTurn = scene; pageTries = 0; strayed = {};
    settleUntil = now + (scene ? 2500u : 300u);     // (until then a page it says is its answer to this, not a hand's turn)
    answerBy = now + (scene ? 2500u : 1100u);
    againLeft = 2; againAt = now + (scene ? 400u : 250u);
    arrived (w);
}

// A turn it has not answered: the page is said again by itself, a moment later and once more
// after that (after a change of scene its script may not be there to say the scene is up; within
// a scene the first may have come at a bad moment). An E16 that answers is sent nothing twice.
// Left unanswered by one whose script does speak, SETUP's E16 line says so.
void Midi::settle (juce::uint32 now)
{
    if (! settling) return;
    if (againLeft > 0 && (juce::int32) (now - againAt) >= 0)
    {
        sendE16 (juce::MidiMessage::programChange (pcCh, asked.page - 1 + pageFirst));
        --againLeft; againAt = now + (sceneTurn ? 800u : 450u);
        if (sceneTurn) arrived (asked);             // (and its values and names again: what was sent while the scene loaded did not arrive either)
    }
    if ((juce::int32) (now - answerBy) < 0) return;
    settling = false; againLeft = 0;
    if (strayed.scene >= 0)
    {
        pcNote = "ASKED FOR " + pageName (asked) + ", IT WENT TO " + pageName (strayed);
        if (! sceneTurn && strayed.scene != asked.scene) pcNote += " (THE E16'S PC MODE MUST BE PAGE)";       // (a Program Change by itself changed its scene)
        arrived (strayed);
    }
    else if (e16Speaks) pcNote = "ASKED FOR " + pageName (asked) + ": NO ANSWER (E16 PC RECEIVE CHANNEL " + String (pcCh) + ", PC MODE PAGE?)";
}

// OPEN (knob 14 pressed): CONFIG for what is in front, and shut again; behind the snapshot knobs
// is the library, and in the library it is the way back.
void Midi::openPressed()
{
    if (ses.showing != 0) { ses.wantShowing = 0; return; }
    if (! ses.cfgOpen && ses.front.kind == Session::fSnap) { ses.wantShowing = 1; return; }
    ses.configOfFront (2);
}

int Midi::cfgPagesNow() { return juce::jlimit (1, (int) e16::kCfgPages, ((int) cfgCache.size() + e16::kCfgPer - 1) / e16::kCfgPer); }

// ------------------------------------------------------------------------- the pointer's wheel
void Midi::wheelStart (juce::uint32 now)
{
    wheelList = ses.stops();
    wheelIdx = juce::jmax (0, Session::stopIndex (wheelList, ses.stopNow()));
    WheelKnob k;
    const bool pages = wheelKnob (k) && ! k.own;
    if (pages)
    {
        // (the page's own knob until now: it stands at its own value, and the counting starts there if it does not take the wheel's)
        wheelAddr = k.ch * 128 + k.num;
        wheelHeard = k.lib ? libSent[12].load() : sent[(size_t) k.slot].load();
    }
    wheelMode = true; wheelRest = 0; wheelBusyAt = now; wheelTries = 0; wheelTold = -1;
    wheelFresh = pages;                              // (a knob that is nothing but the wheel is kept where the pointer would be taken up from)
    if (ses.showing != 0) ses.wantShowing = 0;      // (the pointer is the main screen's)
    ses.point (wheelList[(size_t) wheelIdx]);
}

// The pointer is put down, with nothing chosen.
void Midi::wheelEnd()
{
    ses.pointOff();
    wheelMode = false; wheelRest = 0; wheelTries = 0;
    // the knobs that were the wheel are their pages' own again: each is told its own value
    for (int l = 0; l < kLanes; ++l) sent[(size_t) (l * e16::kPerLane + 12)] = -1;
    sent[(size_t) (kLanes * e16::kPerLane + 12)] = -1;
    libSent[12] = -1;
}

// The choice: what the pointer is on comes to the front (and the E16 turns to it).
void Midi::wheelChoose()
{
    const bool was = ses.pointing;
    const Session::Stop at = ses.pointAt;
    wheelEnd();
    if (was) ses.bringStop (at, 2);
}

void Midi::serviceWheel (juce::uint32 now)
{
    // knobs 13 and 14 held, on the pages where a press recalls
    for (int i = 0; i < 4; ++i)
        if (const auto since = heldSince[(size_t) i].load(); since != 0 && (juce::int32) (now - since) >= 400)
        {
            bool no = false;
            if (heldFired[(size_t) i].compare_exchange_strong (no, true) && heldSince[(size_t) i].load() != 0) (i % 2 == 0 ? wheelKeys : openKeys).fetch_add (1);
        }
    const bool keyed = (wheelKeys.exchange (0) & 1) != 0;
    const int turned = wheelTurn.exchange (0);
    const bool other = otherTurned.exchange (false);
    bool on = wheelMode.load();
    // A press takes the pointer up; a press with it up is the choice.
    if (keyed) { if (on) wheelChoose(); else wheelStart (now); on = ! on; }
    else if (! on && turned != 0) { wheelStart (now); on = true; }      // (the wheel's own address: a turn takes it up)
    else if (on && other && turned == 0) { wheelEnd(); on = false; }    // (the hand has gone to another knob: nothing is chosen)
    if (! on) return;
    if (turned != 0 && ! wheelList.empty())
    {
        // so many clicks to the next, counted from wherever the knob is (fewer when there are more
        // stops than the knob's travel has room for at three)
        const int per = wheelPer ((int) wheelList.size());
        wheelRest += turned;
        const int steps = wheelRest / per;
        wheelRest -= steps * per;
        if (steps != 0)
        {
            const int to = juce::jlimit (0, (int) wheelList.size() - 1, wheelIdx + steps);
            if (to != wheelIdx + steps) wheelRest = 0;                  // (the end of them)
            wheelIdx = to;
            ses.point (wheelList[(size_t) wheelIdx]);
        }
        wheelBusyAt = now;
    }
    if (now - wheelBusyAt > 8000u) wheelEnd();
}

// Knob 13 of the page the E16 is on.
bool Midi::wheelKnob (WheelKnob& k) const
{
    k = {};
    if (e16At.scene == 1 && e16At.page >= e16::pgFind) { k.ch = e16::kSnapChannel; k.num = e16::kWheelCc; k.own = true; }       // (a page where knob 13 is the wheel and nothing else)
    else if (e16At.scene == 1 && e16At.page == e16::pgMap) { k.ch = e16::kMapChannel; k.num = e16::kKnobCc + 12; k.lib = true; }
    else if (e16At.scene == 0 && e16At.page >= 1 && e16At.page <= e16::kSnapPage)
    {
        k.slot = e16At.page == e16::kSnapPage ? kLanes * e16::kPerLane + 12 : (e16At.page - 1) * e16::kPerLane + 12;
        e16::addressOf (k.slot, k.ch, k.num);
    }
    else return false;
    return true;
}

// The wheel's knob is given a number for where the pointer stands: one number to a click, the
// list set in the middle of the knob's travel, so that from any stop there are clicks enough
// left to reach either end (a knob stops at 0 and at 127 and has nothing more to say there). Its
// ring moves with the pointer. Set when the pointer is taken up, and again only when the knob has
// got some way off (it did not take the number, or was turned on past an end): a number sent to
// a knob in a hand is best not sent. Sent a moment after the hand stops, and twice more a little
// later. It does not count as a turn.
void Midi::placeWheel (juce::uint32 now)
{
    const bool on = wheelMode.load();
    WheelKnob k;
    if (! wheelKnob (k)) return;
    if (! on && ! k.own) return;                    // (a page's own knob, with the pointer down: its own value, which is sent with the rest)
    if (k.own && bWait > 0) return;
    const auto all = on ? wheelList : ses.stops();
    const int n = (int) all.size(), idx = on ? wheelIdx : juce::jmax (0, Session::stopIndex (all, ses.stopNow()));
    const int per = wheelPer (n);
    const int want = n > 1 ? (127 - per * (n - 1)) / 2 + per * idx : 64;
    const int address = k.ch * 128 + k.num;
    const bool same = wheelAddr.load() == address;
    const int h = same ? wheelHeard.load() : -1, t = same ? wheelTold.load() : -1;
    // (it is where it should be, near enough, and has not been told otherwise)
    if (! wheelFresh && t < 0 && h >= 0 && std::abs (h - want) <= 6) { wheelTries = 0; return; }
    // (it has spoken since it was last told, or the pointer has moved, or this is another page's knob: begin again)
    if (! same || t != want) wheelTries = 0;
    if (wheelTries >= 3) return;
    static const juce::uint32 gap[3] = { 150, 300, 550 };
    const juce::uint32 last = wheelTries == 0 ? wheelHeardAt.load() : wheelPlacedAt;
    if ((juce::int32) (now - last) < (juce::int32) gap[wheelTries]) return;
    sendE16 (juce::MidiMessage::controllerEvent (k.ch, k.num, want));
    if (wheelAddr.exchange (address) != address) wheelHeard = -1;
    wheelTold = want; wheelPlacedAt = now; ++wheelTries; wheelFresh = false;
    if (k.slot >= 0) sent[(size_t) k.slot] = want;
    if (k.lib) libSent[12] = want;
}

// ------------------------------------------------- the second scene's page that is up: its values
std::array<Midi::PageKnob, 16> Midi::pageKnobs (int page)
{
    std::array<PageKnob, 16> k {};
    const int l = ses.activeLane;
    auto control = [&] (int i, int channel, int number, int ctl)
    {
        const auto& c = engine.control (ctl);
        k[(size_t) i] = { channel, number, c.used ? e16::to7 (c, engine.target (ctl)) : 0 };
    };
    if (page >= e16::pgIns && page < e16::pgIns + kSlots)
        for (int c = 0; c < kCells; ++c) control (c, e16::kFxChannel, e16::kInsCc + (page - e16::pgIns) * kCells + c, cx::ins (l, page - e16::pgIns, c));
    else if (page == e16::pgRet)
        for (int i = 0; i < 4; ++i)
        {
            control (i, e16::kSnapChannel, ccx::kDub + i, cx::kRet + i);
            control (4 + i, e16::kSnapChannel, ccx::kSpace + i, cx::space (engine.spaceModel(), i));
        }
    else if (page == e16::pgMaster)
        for (int i = 0; i < 3; ++i) control (i, e16::kSnapChannel, ccx::kMaster + i, cx::kMaster + i);
    else if (page == e16::pgSeq)
    {
        k[0] = { e16::kFxChannel, e16::kLenCc, juce::jlimit (0, 127, ses.pattern (l).length - 1) };
        for (int j = 0; j < 4; ++j) control (1 + j, e16::kFxChannel, e16::kGenCc + j, cx::gen (l, j));
    }
    else if (page >= e16::pgCfg && page < e16::pgCfg + e16::kCfgPages)
        for (int i = 0; i < e16::kCfgPer; ++i)
        {
            const int idx = (page - e16::pgCfg) * e16::kCfgPer + i;
            if (idx >= (int) cfgCache.size()) continue;
            const auto& sp = cfgCache[(size_t) idx];
            // A switch and a list take a share of the knob's travel each: a knob that is already
            // somewhere in the right share is left where the hand put it (put back to the middle
            // of its share at every pause, a slow turn would never get out of it).
            const int has = bSent[(size_t) i].load(), n = sp.choices.size();
            int want = juce::jlimit (0, 127, (int) std::lround (sp.value * 127.0f));
            if (sp.kind == Kind::Hold) want = 0;
            else if (sp.kind == Kind::Toggle) want = has >= 0 && (has >= 64) == (sp.value > 0.5f) ? has : sp.value > 0.5f ? 127 : 0;
            else if (sp.kind == Kind::List && n > 1)
            {
                const int at = juce::jlimit (0, n - 1, (int) std::lround (sp.value * (float) (n - 1)));
                want = has >= 0 && juce::jmin (n - 1, has * n / 128) == at ? has : juce::jlimit (0, 127, (at * 128 + 64) / n);
            }
            else if (sp.kind == Kind::Bipolar) want = juce::jlimit (0, 127, (int) std::lround (sp.value <= 0.5f ? sp.value * 128.0f : 64.0f + (sp.value - 0.5f) * 126.0f));
            k[(size_t) i] = { e16::kFxChannel, e16::cfgCc (idx), want };
        }
    return k;
}

void Midi::feedPage (juce::uint32 now)
{
    if (e16At.scene != 1 || e16At.page < e16::pgIns || bWait > 0) return;
    const auto knobs = pageKnobs (e16At.page);
    for (int i = 0; i < 12; ++i)                // (the bottom row is the wheel's and the keys')
    {
        const auto& k = knobs[(size_t) i];
        if (k.number < 0 || k.want < 0 || k.want == bSent[(size_t) i].load() || now - bHeardAt[(size_t) i].load() < 150u) continue;
        sendE16 (juce::MidiMessage::controllerEvent (k.channel, k.number, k.want));
        bSent[(size_t) i] = k.want;
    }
}

// ----------------------------------------------------------------- the page's names, to the script
// What a page of the second scene holds depends on what is in front (an insert's eight places,
// CONFIG's controls): the script is sent a title and sixteen names of four characters, and the
// E16 shows them in place of the scene's own. With the wheel in use the title is what the pointer
// is on, on any page. Sent when they change.
//   F0 7D 53 46 04 {page, sixteen names} F7     (a name of four spaces: the scene's own)
//   F0 7D 53 46 05 {title} F7                   (none: the scene's own)
void Midi::sayNames()
{
    if (e16At.page < 1) return;
    String title;
    std::array<String, 16> nm;
    const int pg = e16At.page, l = ses.activeLane;
    auto four = [] (const String& in) { return e16::cleaned (in.removeCharacters (" "), 4); };
    if (e16At.scene == 1 && pg >= e16::pgIns && pg < e16::pgIns + kSlots)
    {
        const int slot = pg - e16::pgIns, t = engine.insertType (l, slot);
        title = String (e16::laneCodes[l]) + " " + String (slot + 1) + " " + (t >= 0 ? effects()[(size_t) t].name : "EMPTY");
        for (int c = 0; c < kCells; ++c)
        {
            String name, abbr ("-");
            if (t >= 0 && effects()[(size_t) t].cells[c].label != nullptr) e16::naming (effects()[(size_t) t].cells[c], name, abbr);
            nm[(size_t) c] = abbr;
        }
    }
    else if (e16At.scene == 1 && pg == e16::pgSeq) title = String (e16::laneCodes[l]) + " SEQUENCER";
    else if (e16At.scene == 1 && pg >= e16::pgCfg && pg < e16::pgCfg + e16::kCfgPages)
    {
        const int c = pg - e16::pgCfg, pages = cfgPagesNow();
        if (ses.cfgOpen)
        {
            const auto& t = ses.cfg;
            const int fx = t.kind == 1 ? engine.insertType (t.lane, t.slot) : -1;
            title = t.kind == 0 ? String (e16::laneCodes[t.lane]) : t.kind == 1 ? String (e16::laneCodes[t.lane]) + " " + (fx >= 0 ? effects()[(size_t) fx].name : "EMPTY") : String (t.slot == 0 ? "DUB" : "SPACE");
            title = title.substring (0, pages > 1 ? 7 : 11) + " CFG" + (pages > 1 ? " " + String (c + 1) + "/" + String (pages) : String());
        }
        for (int i = 0; i < e16::kCfgPer; ++i)
        {
            const int idx = c * e16::kCfgPer + i;
            nm[(size_t) i] = ses.cfgOpen && idx < (int) cfgCache.size() ? four (cfgCache[(size_t) idx].name) : String ("-");
        }
    }
    if (wheelMode.load() && ses.pointing) { title = "> " + ses.stopName (ses.pointAt); nm[12] = "SEL"; }

    title = e16::cleaned (title, 15);
    if (title == "-") title = {};
    String names;
    for (auto& n : nm) names << (n.isEmpty() ? String ("    ") : n.paddedRight (' ', 4).substring (0, 4));
    const String key = String (e16At.scene) + "/" + String (pg) + "/" + title + "/" + names;
    if (key == namesSaid) return;
    const bool first = namesSaid.isEmpty();
    namesSaid = key;
    if (first && title.isEmpty() && names.trim().isEmpty()) return;      // (a page with its own names, newly come up: nothing to say)
    std::vector<juce::uint8> msg { 0x7D, 0x53, 0x46, 0x04, (juce::uint8) pg };
    for (int i = 0; i < names.length(); ++i) msg.push_back ((juce::uint8) (names[i] & 0x7f));
    sendE16 (juce::MidiMessage::createSysExMessage (msg.data(), (int) msg.size()));
    msg = { 0x7D, 0x53, 0x46, 0x05 };
    for (int i = 0; i < title.length(); ++i) msg.push_back ((juce::uint8) (title[i] & 0x7f));
    sendE16 (juce::MidiMessage::createSysExMessage (msg.data(), (int) msg.size()));
}

// GLIDE from outside (the E16: knob 1 of a lane's page, pressed, or knob 16 of the library's FIND; a sequencer)
void Midi::takeGlide()
{
    if (const int g = glideIn.exchange (-1); g >= 0)
    {
        ses.userSet (cx::kGlide, e16::from7 (engine.control (cx::kGlide), g));
        if (glideFromE16.load()) { glideSent = g; glideHeardAt = juce::Time::getMillisecondCounter(); }       // (a sequencer's GLIDE goes on to the E16)
    }
}

// RECALL from a sequencer. A GLIDE sent with it is taken first, so the recall travels by it.
void Midi::takeRecalls()
{
    for (int k = 0; k < 16; ++k)
        if (const int n = recallIn[(size_t) k].exchange (0); n > 0) { takeGlide(); ses.recallFromKnob (k, n); }
}

void Midi::handleAsyncUpdate() { takeRecalls(); }

// The snapshots a knob of the library's scene holds.
std::vector<juce::int64> Midi::libFor (int k)
{
    if (k < 16) return ses.mapCell (3 - k / 4, k % 4);              // the top row of knobs is the top of the map
    if (k == 16) return ses.recent (32);
    if (k == 17) return ses.fitting (ses.heardNow(), Session::Fit::like, 12);
    if (k == 18) return ses.fitting (ses.heardNow(), Session::Fit::more, 12);
    if (k == 19) return ses.fitting (ses.heardNow(), Session::Fit::less, 12);
    return {};
}

// The library's scene: a turn plays what the knob comes to in the headphones, a press recalls it.
void Midi::serviceLibrary()
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool refresh = ++libTick % 20 == 1;                       // twice a second, for the rings
    for (int k = 0; k < e16::kLibKnobs; ++k)
    {
        const auto i = (size_t) k;
        const int v = libLatest[i].exchange (-1);
        const int turned = libTurn[i].exchange (0);
        const bool pressed = libPresses[i].exchange (0) > 0;
        // What a knob holds is worked out when a hand comes to it, and stays put while the hand
        // is there: the list does not shift under it as the music moves or a snapshot is taken.
        // (A knob no hand has been on for a while is looked at again twice a second, for its ring.)
        const bool away = now - libTouched[i] > 4000;
        if (away && (v >= 0 || pressed || refresh)) libList[i] = libFor (k);
        if (v >= 0 || pressed) libTouched[i] = now == 0 ? 1u : now;
        const auto& list = libList[i];
        const int n = (int) list.size();
        if (v >= 0 && n > 0)
        {
            // so many clicks to a step; the snapshot it is on stays until a step is made
            const int per = n > 1 ? juce::jlimit (1, 4, 119 / (n - 1)) : 4;
            libRest[i] += turned;
            const int steps = libRest[i] / per;
            libRest[i] -= steps * per;
            const auto at = std::find (list.begin(), list.end(), libPick[i]);
            int idx = at != list.end() ? (int) (at - list.begin()) : -1;
            if (idx < 0) { idx = 0; libRest[i] = 0; }                          // it held none of these: the first
            else if (steps != 0)
            {
                const int to = juce::jlimit (0, n - 1, idx + steps);
                if (to != idx + steps) libRest[i] = 0;                         // (the end of them)
                idx = to;
            }
            const juce::int64 id = list[(size_t) idx];
            if (id != libPick[i] || ses.cueing() != id) { libPick[i] = id; ses.cue (id); }
        }
        if (pressed)
        {
            juce::int64 id = std::find (list.begin(), list.end(), libPick[i]) != list.end() ? libPick[i] : (n > 0 ? list.front() : 0);
            if (ses.findSnap (id) != nullptr) { libPick[i] = id; ses.recall (id); ses.cue (0); }
        }
    }
    if (! toE16 && e16Out == nullptr) return;
    // each ring sits in the middle of the place of the snapshot its knob holds (at nought with none)
    for (int k = 0; k < e16::kLibKnobs; ++k)
    {
        const auto i = (size_t) k;
        const auto& list = libList[i];
        const int n = (int) list.size();
        const auto at = std::find (list.begin(), list.end(), libPick[i]);
        if (at == list.end() && libPick[i] != 0) continue;                 // what it is on is not among these now: its ring stays where it is
        const int idx = at != list.end() ? (int) (at - list.begin()) : 0;
        // where it stands among them: the first a little up from nought (nought is an empty knob), the last at the top; one alone, the middle
        const int want = n > 1 ? juce::jlimit (0, 127, 8 + (idx * 119 + (n - 1) / 2) / (n - 1)) : n == 1 ? 64 : 0;
        if (want == libSent[i].load() || now - libHeardAt[i].load() < 150u) continue;
        if (k == 12 && wheelMode.load()) continue;                         // (the pointer's wheel for now)
        sendE16 (juce::MidiMessage::controllerEvent (k < 16 ? e16::kMapChannel : e16::kFindChannel, e16::kKnobCc + k % 16, want));
        libSent[i] = want;
    }
}

// GLIDE RINGS: the travelling knobs to the scene's script, as SysEx (the suite's E16Glide.h).
//   F0 7D 53 46 01 {channel - 1, number, value / 128, value % 128} x up to 14  F7   where the sound is (0-16383)
//   F0 7D 53 46 02 {channel - 1, number} x up to 28                              F7   arrived
// A knob is reported when it sets off, whenever its ring has moved by 32 or more, and at least
// every 400 ms: the script lets a ring go after 1.2 s without a report.
void Midi::rings (double nowMs)
{
    std::vector<juce::uint8> msg;
    int count = 0;
    auto open = [&] (juce::uint8 kind) { msg = { 0x7D, 0x53, 0x46, kind }; count = 0; };
    auto close = [&] { if (count > 0) sendE16 (juce::MidiMessage::createSysExMessage (msg.data(), (int) msg.size())); };
    std::array<bool, e16::kSlots> on {};
    open (1);
    for (int slot = 0; slot < kLanes * e16::kPerLane; ++slot)
    {
        if (slot % e16::kPerLane >= 15) continue;                  // HIGH and MID share a knob: it keeps its own ring
        const int ctl = e16::controlFor (slot);
        if (! engine.isGliding (ctl)) continue;
        on[(size_t) slot] = true;
        const int p = juce::jlimit (0, 16383, (int) std::lround (e16::pos7 (engine.control (ctl), engine.shown (ctl)) * 16383.0 / 127.0));
        if (ringOn[(size_t) slot] && std::abs (p - ringLast[(size_t) slot]) < 32 && nowMs - ringSentAt[(size_t) slot] < 400.0) continue;
        if (count == 14) { close(); open (1); }
        int ch, num;
        e16::addressOf (slot, ch, num);
        msg.insert (msg.end(), { (juce::uint8) (ch - 1), (juce::uint8) num, (juce::uint8) (p >> 7), (juce::uint8) (p & 127) });
        ++count;
        ringLast[(size_t) slot] = p; ringSentAt[(size_t) slot] = nowMs;
    }
    close();
    open (2);
    for (int slot = 0; slot < kLanes * e16::kPerLane; ++slot)
    {
        if (! ringOn[(size_t) slot] || on[(size_t) slot]) continue;
        if (count == 28) { close(); open (2); }
        int ch, num;
        e16::addressOf (slot, ch, num);
        msg.insert (msg.end(), { (juce::uint8) (ch - 1), (juce::uint8) num });
        ++count;
    }
    close();
    ringOn = on;
}

// ==================================================================================== the clock
// Each tick goes out at its own moment: the engine says when (a block ahead at least), this thread
// sleeps to a quarter of a millisecond before it and stays awake for the rest. With no clock to
// send it looks only now and then.
void Midi::run()
{
    Engine::ClockMsg m;
    bool have = false;
    while (! threadShouldExit())
    {
        if (! have) have = engine.nextClock (m);
        if (! have) { wait (engine.clockOut() && engine.isRunning() ? 1 : 10); continue; }
        const double left = m.dueMs - juce::Time::getMillisecondCounterHiRes();
        if (left > 60.0) { have = false; continue; }               // not this clock's (the device was restarted): let it go
        if (left > 0.35) { std::this_thread::sleep_for (std::chrono::microseconds ((long long) ((left - 0.25) * 1000.0))); continue; }
        while (juce::Time::getMillisecondCounterHiRes() < m.dueMs && ! threadShouldExit()) juce::Thread::yield();
        {
            const juce::ScopedLock g (outLock);
            for (auto& o : clockOuts) o->sendMessageNow (juce::MidiMessage ((int) m.byte));
        }
        have = false;
    }
}
} // namespace sf0

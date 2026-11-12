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
        { "width", "WIDE" }, { "echo_time", "ETIM" }, { "echo_fb", "EFBK" } };
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
    static const char* const codes[kLanes] = { "KCK", "CHH", "OHH", "CLP", "PRC", "BAS", "RMB", "LED", "CRD", "PAD", "VOX" };
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
            else if (p == 13) { e.name = "SEND A"; e.abbr = "SNDA"; e.colour = kLightBlue; }
            else if (p == 14) { e.name = "SEND B"; e.abbr = "SNDB"; e.colour = kLightBlue; }
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

// The library's scene: the map on page 1, the finders on page 2, the rest empty. It carries the
// first scene's script (which finds nothing of its own to do here), so its form is one the E16
// is known to take.
String libraryText()
{
    std::vector<std::string> pages;
    String map;
    juce::StringArray focus;
    static const char* const rows[4] = { "PEAK", "DRIVE", "EASY", "CALM" };         // the top row of knobs first
    static const char* const cols[4] = { "DK", "WM", "CL", "BR" };                  // dark, warm, clear, bright
    static const int colColour[4] = { kRed, kPink, kLightBlue, kWhite };
    {
        std::vector<std::string> encs;
        for (int p = 1; p <= 16; ++p)
        {
            Enc e;
            const int r = (p - 1) / 4, c = (p - 1) % 4;
            e.name = String (rows[r]) + " " + cols[c]; e.abbr = String (rows[r]).substring (0, 2) + cols[c];
            e.colour = colColour[c];
            e.channel = kMapChannel; e.number = kKnobCc + p - 1; e.def = 0;
            e.press = 1; e.pressChannel = kMapChannel; e.pressNumber = kPressCc + p - 1;
            encs.push_back (encJson (e, p));
            map << "---";
        }
        pages.push_back (Json::obj ({ { "title", Json::str ("MAP") }, { "output", "0" }, { "channel", Json::num (kMapChannel) }, { "encoders", Json::arr (encs) } }));
        focus.add ("0");
    }
    {
        static const char* const names[4] = { "RECENT", "FITS NOW", "MORE", "LESS" };
        static const char* const abbrs[4] = { "RCNT", "FITS", "MORE", "LESS" };
        static const int colours[4] = { kViolet, kGreen, kWhite, kRed };
        std::vector<std::string> encs;
        for (int p = 1; p <= 16; ++p)
        {
            Enc e;
            if (p <= 4)
            {
                e.name = names[p - 1]; e.abbr = abbrs[p - 1]; e.colour = colours[p - 1];
                e.channel = kFindChannel; e.number = kKnobCc + p - 1; e.def = 0;
                e.press = 1; e.pressChannel = kFindChannel; e.pressNumber = kPressCc + p - 1;
            }
            else if (p == 16) { e.name = "GLIDE"; e.abbr = "GLID"; e.colour = kGreen; e.channel = kSnapChannel; e.number = kGlideCc; e.def = 0; }
            encs.push_back (encJson (e, p));
            map << "---";
        }
        pages.push_back (Json::obj ({ { "title", Json::str ("FIND") }, { "output", "0" }, { "channel", Json::num (kFindChannel) }, { "encoders", Json::arr (encs) } }));
        focus.add ("0");
    }
    for (int page = 3; page <= 12; ++page)
    {
        std::vector<std::string> encs;
        for (int p = 1; p <= 16; ++p) { encs.push_back (encJson (Enc(), p)); map << "---"; }
        pages.push_back (Json::obj ({ { "title", Json::str ("-") }, { "output", "0" }, { "channel", "1" }, { "encoders", Json::arr (encs) } }));
        focus.add ("0");
    }
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
        { "title", Json::str ("SF-LIB") }, { "icon", Json::arr (icon) }, { "selectedPreset", "0" }, { "color", Json::num (kViolet) },
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

void Midi::incoming (const juce::MidiMessage& m, bool fromE16)
{
    if (m.isNoteOnOrOff())
    {
        if (! fromE16) engine.noteIn (m, juce::Time::getMillisecondCounterHiRes());
        return;
    }
    if (! m.isController()) return;
    const int ch = m.getChannel(), num = m.getControllerNumber(), val = m.getControllerValue();
    if (ch == e16::kMapChannel || ch == e16::kFindChannel)
    {
        const int base = ch == e16::kMapChannel ? 0 : 16;
        if (num >= e16::kKnobCc && num < e16::kKnobCc + 16)
        {
            const auto k = (size_t) (base + num - e16::kKnobCc);
            // how far it was turned, from where the knob was (its number is the E16's own business:
            // SF-000 moves it to show a place, and that must not count as a turn)
            if (const int was = libSent[k].exchange (val); was >= 0) libTurn[k].fetch_add (val - was);
            libLatest[k] = val; libHeardAt[k] = juce::Time::getMillisecondCounter();
        }
        else if (num >= e16::kPressCc && num < e16::kPressCc + 16 && val >= 64) libPresses[(size_t) (base + num - e16::kPressCc)].fetch_add (1);
        return;
    }
    if (ch == e16::kSnapChannel)
    {
        if (num == e16::kFocusCc) { if (val >= 1 && val <= kLanes) focus = val; if (val >= 1 && val <= e16::kSnapPage) pageUp = val; return; }
        if (num == e16::kGlideCc) { glideFromE16 = fromE16; glideIn = val; ses.moved (cx::kGlide, fromE16 ? 1 : 2); return; }
        if (num >= e16::kPressCc && num < e16::kPressCc + 16) { if (val >= 64) presses[(size_t) (num - e16::kPressCc)].fetch_add (1); return; }
        if (num >= ccx::kRecall && num < ccx::kRecall + 16)
        {
            // RECALL: which of the snapshots a snapshot knob holds. Only a new value acts.
            const auto k = (size_t) (num - ccx::kRecall);
            if (recallHeard[k].exchange (val) != val && val > 0) { recallIn[k] = val; if (useDevices) triggerAsyncUpdate(); }
            return;
        }
    }
    // (an insert's place is whatever effect is in it when the message thread comes to it)
    if (const int extra = ccx::extraFor (ch, num); extra >= 0) { exBy[(size_t) extra] = fromE16 ? 1 : 2; exLatest[(size_t) extra] = val; return; }
    const int slot = e16::slotFor (ch, num);
    if (slot < 0) return;
    const int ctl = e16::controlFor (slot);
    if (fromE16) { sent[(size_t) slot] = val; heardAt[(size_t) slot] = juce::Time::getMillisecondCounter(); }
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
    if (const int f = focus.exchange (0); f > 0) ses.selectLane (f - 1);
    const auto now = juce::Time::getMillisecondCounter();
    takeGlide();
    takeRecalls();
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
    // A page that has just come up is told its values again, a moment after it has settled.
    if (const int pg = pageUp.exchange (0); pg > 0) { pageNow = pg; pageWait = 2; }
    if (pageWait > 0 && --pageWait == 0)
    {
        const int first = (pageNow - 1) * e16::kPerLane, count = pageNow == e16::kSnapPage ? 16 : e16::kPerLane;
        for (int k = 0; k < count && first + k < e16::kSlots; ++k) sent[(size_t) (first + k)] = -1;
        glideSent = -1;
    }
    serviceLibrary();

    if (! toE16 && e16Out == nullptr) return;
    // The E16's rings follow the rig: a value that changed here (a recall, the mouse, a snapshot
    // knob's push) is sent to its address. A knob the hand is on is left alone for a moment.
    // No more than a couple of dozen at a time (a fresh E16 is told some two hundred, and a burst
    // of that size can be more than it takes in), the page that is up first.
    int room = 24;
    const int from = pageNow > 0 ? (pageNow - 1) * e16::kPerLane : 0;
    for (int k = 0; k < e16::kSlots && room > 0; ++k)
    {
        const int slot = (from + k) % e16::kSlots;
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
    rings (juce::Time::getMillisecondCounterHiRes());
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

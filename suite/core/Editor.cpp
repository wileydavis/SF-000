#include "Editor.h"
#include <map>

using namespace sf;

namespace
{
// A ring's colour from the OXI App's colour number, as the E16 lights it (E16Map.h kE16Rings): white,
// red, light blue, pink, violet, green, by role (layout.py).
juce::Colour ringColour (int c) { return juce::Colour (0xff000000u | e16::ringRGB (c)); }

// A label squeezed (to 75 %) rather than cut when it is long.
void fitted (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c, float h,
             juce::Justification j = juce::Justification::centred, bool bold = false)
{
    g.setColour (c);
    g.setFont (ui::mono (h, bold));
    g.drawFittedText (s, r.toNearestInt(), j, 1, 0.75f);
}

juce::String onOff (bool on) { return on ? "ON" : "OFF"; }
} // namespace

// ---------------------------------------------------------------- look and feel
SFLookAndFeel::SFLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, ui::kBg);
    setColour (juce::ComboBox::textColourId, ui::kText);
    setColour (juce::ComboBox::outlineColourId, ui::kLine);
    setColour (juce::ComboBox::arrowColourId, ui::kDim);
    setColour (juce::PopupMenu::backgroundColourId, ui::kPanel);
    setColour (juce::PopupMenu::textColourId, ui::kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::kText);
    setColour (juce::PopupMenu::highlightedTextColourId, ui::kBg);
    setColour (juce::TextButton::buttonColourId, ui::kBg);
    setColour (juce::TextButton::buttonOnColourId, ui::kText);
    setColour (juce::TextButton::textColourOffId, ui::kText);
    setColour (juce::TextButton::textColourOnId, ui::kBg);
}

void SFLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState() || down;
    const auto onColour = b.getProperties().contains ("onColour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"]) : ui::kText;
    g.setColour (on ? onColour : ui::kBg);
    g.fillRect (r);
    g.setColour (over && ! on ? ui::kDim : ui::kLine);
    g.drawRect (r, 1.0f);
}

void SFLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool on = b.getToggleState() || down;
    const float h = b.getHeight() <= 13 ? 8.0f : b.getHeight() <= 24 ? 9.5f : 11.0f;
    fitted (g, b.getButtonText(), b.getLocalBounds().toFloat().reduced (2.0f, 0.0f), on ? ui::kBg : ui::kText, h, juce::Justification::centred, true);
}

void SFLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (ui::kBg);
    g.fillRect (r);
    g.setColour (box.hasKeyboardFocus (false) ? ui::kDim : ui::kLine);
    g.drawRect (r, 1.0f);
    juce::Path p;
    const float ax = (float) w - 7.0f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 3, ay - 1.5f, ax + 3, ay - 1.5f, ax, ay + 2.5f);
    g.setColour (ui::kDim);
    g.fillPath (p);
}

void SFLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (2, 0, box.getWidth() - 13, box.getHeight());
    l.setFont (getComboBoxFont (box));
    l.setMinimumHorizontalScale (0.7f);
    l.setBorderSize ({ 1, 2, 1, 1 });
    l.setColour (juce::Label::textColourId, ui::kText);
}

// ---------------------------------------------------------------- cells
// One control: a knob (an E16 encoder's ring and cap), a menu, or a switch.
// A knob's slider: a right-click (or ctrl-click) locks or unlocks the control for the randomiser,
// and turns nothing.
struct LockSlider : juce::Slider
{
    using juce::Slider::Slider;
    std::function<void()> onLock;
    bool locking = false;
    void mouseDown (const juce::MouseEvent& e) override
    {
        locking = e.mods.isPopupMenu() && onLock != nullptr;
        if (locking) { onLock(); return; }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! locking) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! locking) juce::Slider::mouseUp (e); locking = false; }
};

class SFCell : public juce::Component
{
public:
    enum class Kind { Ring, Menu, Switch };
    struct Look
    {
        bool big = false;            // top-row size (the E16 grid, PERFORM)
        bool e16 = false;            // an E16 encoder: fills from the first LED whatever the parameter
        juce::Colour led = ui::kText;
        float top = 1.0f;            // the ring is full here (normalised): the top of Live's mapping
        int press = -1;              // the encoder's press (E16 grid): a parameter
        juce::String name;           // empty: the parameter's own
        bool narrow = false;         // a menu in one column however long its choices
    };
    static constexpr float kNameH = 11.0f;

    SFCell (SFProcessor& p, int index, Look lk) : proc (p), idx (index), look (std::move (lk))
    {
        const auto& s = p.allSpecs()[(size_t) idx];
        if (look.name.isEmpty()) look.name = s.name;
        if (s.id == "m1_src") motionIndex = 0;
        if (s.id == "m2_src") motionIndex = 1;
        // A knob whose middle is neutral fills from the top, either way (an E16 encoder always fills
        // from its first LED, as the E16 does): BIP / SLOT knobs, and ranges either side of 0 (TUNE).
        if (! look.e16 && s.type == PType::Float)
        {
            if (s.unit == "BIP" || s.unit == "SLOT" || s.unit == "SPLIT") centrePos = 0.5f;
            else if (s.min < 0.0f && s.max > 0.0f && std::abs (s.min + s.max) < 1e-3f * (s.max - s.min))
                centrePos = p.param (idx)->convertTo0to1 (0.0f);
        }
        auto& ap = p.apvts;
        if (s.type == PType::Float || (look.e16 && s.type == PType::Choice))
        {
            kind = Kind::Ring;
            auto ls = std::make_unique<LockSlider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            ls->onLock = [this] { if (proc.randomisable (idx)) { proc.setLocked (idx, ! proc.isLocked (idx)); repaint(); } };
            slider = std::move (ls);
            slider->setMouseDragSensitivity (look.big ? 240 : 200);
            slider->setVelocityModeParameters (1.0, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
            sAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ap, s.id, *slider);
            slider->setDoubleClickReturnValue (true, s.def);
            slider->onValueChange = [this] { hotUntil = juce::Time::getMillisecondCounter() + 700; repaint(); };
            slider->setTitle (look.name);
            addAndMakeVisible (*slider);
            if (look.press >= 0) makeButton (look.press, true);
        }
        else if (s.type == PType::Choice)
        {
            kind = Kind::Menu;
            box = std::make_unique<juce::ComboBox>();
            for (size_t i = 0; i < s.choices.size(); ++i) box->addItem (s.choices[i], (int) i + 1);
            cAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, s.id, *box);
            box->setTitle (look.name);
            addAndMakeVisible (*box);
            // One column shows about 70 pixels of text (squeezed to 70 %); longer choices take two.
            float w = 0.0f;
            for (auto& c : s.choices) w = std::max (w, juce::GlyphArrangement::getStringWidth (ui::mono (10.0f), juce::String (c)));
            wide = ! look.narrow && w > 70.0f;
        }
        else
        {
            kind = Kind::Switch;
            makeButton (idx, false);
        }
    }

    // A momentary button held while its window closes is released (PANIC must never stay held).
    ~SFCell() override
    {
        if (held >= 0)
            if (auto* prm = proc.param (held))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (0.0f);
                prm->endChangeGesture();
            }
    }

    Kind getKind() const { return kind; }
    int index() const { return idx; }
    int pressIndex() const { return look.press; }
    int span() const { return wide ? 2 : 1; }
    // The mouse wheel turns the knob, or (off: the strip scrolls) passes to the strip.
    void setWheel (bool on) { if (slider != nullptr) slider->setScrollWheelEnabled (on); }
    int naturalHeight() const
    {
        switch (kind)
        {
            case Kind::Ring:   return look.big ? (look.press >= 0 ? 83 : 70) : 56;
            case Kind::Menu:   return 36;
            case Kind::Switch: break;
        }
        return 34;
    }

    // "NAME VALUE", for a folded group's title line.
    juce::String summary() const
    {
        const auto& s = proc.allSpecs()[(size_t) idx];
        if (s.type == PType::Momentary) return {};
        auto* prm = proc.param (idx);
        const auto v = s.type == PType::Toggle ? onOff (prm->getValue() > 0.5f) : prm->getCurrentValueAsText();
        return juce::String (s.name) + " " + v;
    }

    // The editor's timer: repaint only when what the cell shows has changed.
    void poll()
    {
        if (kind == Kind::Ring)
        {
            auto* prm = proc.param (idx);
            const float t = prm->getValue(), n = prm->convertTo0to1 (proc.baseValue (idx));
            const float m = proc.isModulated (idx) ? prm->convertTo0to1 (proc.currentValue (idx)) : -1.0f;
            const bool hot = slider->isMouseButtonDown() || juce::Time::getMillisecondCounter() < hotUntil;
            if (std::abs (t - shownT) > 1e-5f || std::abs (n - shownN) > 1e-3f || std::abs (m - shownM) > 1e-3f || hot != shownHot)
            {
                shownT = t; shownN = n; shownM = m; shownHot = hot;
                repaint();
            }
        }
        else if (motionIndex >= 0)
        {
            const float v = proc.currentValue (idx) > 0.5f ? proc.motionValue (motionIndex) : -9.0f;
            if (std::abs (v - shownM) > 0.01f) { shownM = v; repaint(); }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (kind != Kind::Switch)
            fitted (g, look.name, r.removeFromTop (kNameH), look.e16 ? ui::kText : ui::kDim, 9.0f);
        if (kind == Kind::Ring) paintRing (g);
        if (kind == Kind::Menu && motionIndex >= 0 && proc.currentValue (idx) > 0.5f)
        {
            // MOTION source: the modulator's output now.
            const float m = proc.motionValue (motionIndex);
            const auto bar = juce::Rectangle<float> (4.0f, (float) getHeight() - 3.0f, (float) getWidth() - 8.0f, 2.0f);
            g.setColour (ui::kTrack);
            g.fillRect (bar);
            const float cx = bar.getCentreX(), x = cx + juce::jlimit (-1.0f, 1.0f, m) * bar.getWidth() * 0.5f;
            g.setColour (ui::kGreen);
            g.fillRect (juce::Rectangle<float> (std::min (cx, x), bar.getY(), std::max (1.0f, std::abs (x - cx)), bar.getHeight()));
        }
    }

    void resized() override
    {
        const int w = getWidth();
        if (slider != nullptr)
        {
            const auto c = centre();
            const int half = (int) std::ceil (radius() + led() * 2.0f + 1.0f);
            slider->setBounds ((int) c.x - half, (int) c.y - half, 2 * half, 2 * half);
            if (button != nullptr) button->setBounds (7, 71, w - 14, 11);
        }
        if (box != nullptr) box->setBounds (3, 13, w - 6, 20);
        if (button != nullptr && kind == Kind::Switch) button->setBounds (3, 6, w - 6, 22);
    }

private:
    juce::Point<float> centre() const { return { (float) getWidth() * 0.5f, look.big ? 35.0f : 27.0f }; }
    float radius() const { return look.big ? 19.0f : 12.5f; }
    float led() const    { return look.big ? 2.15f : 1.5f; }

    void paintRing (juce::Graphics& g)
    {
        auto* prm = proc.param (idx);
        const auto c = centre();
        const float rr = radius(), ld = led();
        ui::drawCap (g, c, look.big ? 10.5f : 7.0f);
        const float target = prm->getValue();
        const float now = prm->convertTo0to1 (proc.baseValue (idx));
        const bool gliding = std::abs (target - now) > 0.003f;
        auto pos = [this] (float v) { return juce::jlimit (0.0f, 1.0f, v / std::max (0.01f, look.top)) * 16.0f; };
        const float from = centrePos >= 0.0f ? pos (centrePos) : 0.0f, p = pos (gliding ? now : target);
        // SLOW: the ring walks green with the sound (as on the E16); the ringed LED is where it goes.
        ui::drawLeds (g, c, rr, ld, std::min (from, p), std::max (from, p), gliding ? ui::kGreen : look.led, ui::kLedOff);
        if (gliding) ui::drawLedMark (g, c, rr, ld, pos (target), ui::kText);
        if (proc.isModulated (idx)) ui::drawModDot (g, c, rr, ld, pos (prm->convertTo0to1 (proc.currentValue (idx))), ui::kGreen);
        // Locked for the randomiser: a small padlock by the ring's top right.
        if (proc.randomisable (idx) && proc.isLocked (idx))
        {
            const float x = c.x + rr + ld + 1.0f, y = c.y - rr - ld + 2.0f;
            g.setColour (ui::kDim);
            g.fillRect (juce::Rectangle<float> (x, y + 3.0f, 6.0f, 4.5f));
            juce::Path sh;
            sh.addCentredArc (x + 3.0f, y + 3.0f, 2.0f, 2.2f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (sh, juce::PathStrokeType (1.1f));
        }
        const bool hot = slider->isMouseButtonDown() || juce::Time::getMillisecondCounter() < hotUntil;
        const float vy = look.big ? 59.0f : 44.0f;
        fitted (g, prm->getCurrentValueAsText(), { 0.0f, vy, (float) getWidth(), 11.0f },
                hot ? ui::kAccent : gliding ? ui::kGreen : ui::kText, look.big ? 10.0f : 9.5f);
    }

    // A switch, or an encoder's press: a toggle, or a momentary held while the mouse is down.
    void makeButton (int param, bool isPress)
    {
        const auto& s = proc.allSpecs()[(size_t) param];
        button = std::make_unique<juce::TextButton> (s.name);
        if (s.type == PType::Toggle)
        {
            button->setClickingTogglesState (true);
            bAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, s.id, *button);
        }
        else
        {
            auto* prm = proc.param (param);
            button->onStateChange = [this, prm, param]
            {
                const bool down = button->isDown();
                if (down == (held >= 0)) return;
                held = down ? param : -1;
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (down ? 1.0f : 0.0f);
                prm->endChangeGesture();
            };
        }
        button->setTitle (isPress ? look.name + " PRESS: " + juce::String (s.name) : juce::String (s.name));
        addAndMakeVisible (*button);
    }

    SFProcessor& proc;
    int idx;
    Look look;
    Kind kind = Kind::Switch;
    bool wide = false;
    float centrePos = -1.0f;         // where a neutral-middle knob's fill starts (normalised; -1: the first LED)
    int held = -1;                   // the momentary parameter this cell's button holds down
    int motionIndex = -1;
    juce::uint32 hotUntil = 0;
    float shownT = -1.0f, shownN = -1.0f, shownM = -2.0f;
    bool shownHot = false;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> box;
    std::unique_ptr<juce::TextButton> button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAttach;
};

// ---------------------------------------------------------------- editor
SFEditor::SFEditor (SFProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    auto name = [] (Group& g, const char* title, int bit, bool foldedAtFirst) { g.title = title; g.bit = bit; g.foldedAtFirst = foldedAtFirst; };
    name (perform, "PERFORM", 0, false);
    name (detail, "DETAIL", kFoldDetail, false);
    name (engine, "ENGINE", kFoldEngine, true);
    name (layer, "LAYER", kFoldLayer, true);
    name (sample, "SAMPLE", kFoldSample, true);
    name (motion, "MOTION", kFoldMotion, true);
    name (system, "SYSTEM", kFoldSystem, true);
    name (setup, "SETUP", kFoldSetup, true);
    storeButton.onClick = [this] { proc.storeSong(); };
    addAndMakeVisible (storeButton);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);
    if (p.mod().acceptsFiles())
    {
        // LOAD: a file, or RESAMPLE [Nov 10]: the plugin's own output from the next bar line.
        loadButton.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "FILE...");
            m.addSeparator();
            const bool busy = proc.resampleStage() != 0;
            m.addItem (2, "RESAMPLE 1 BAR", ! busy);
            m.addItem (3, "RESAMPLE 2 BARS", ! busy);
            m.addItem (4, "RESAMPLE 4 BARS", ! busy);
            if (busy) m.addItem (5, "CANCEL RESAMPLE");
            juce::Component::SafePointer<SFEditor> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loadButton), [self] (int r)
            {
                if (self == nullptr || r == 0) return;
                if (r == 1)
                {
                    self->chooser = std::make_unique<juce::FileChooser> ("LOAD", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
                    self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                                [self] (const juce::FileChooser& fc)
                                                {
                                                    if (self == nullptr) return;
                                                    juce::StringArray paths;
                                                    for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                                    if (! paths.isEmpty()) self->proc.mod().loadFiles (paths);
                                                });
                }
                else if (r >= 2 && r <= 4) self->proc.armResample (r == 2 ? 1 : r == 3 ? 2 : 4);
                else if (r == 5) self->proc.cancelResample();
            });
        };
        addAndMakeVisible (loadButton);
    }
    view.setViewedComponent (&body, false);
    view.setScrollBarsShown (true, false);              // vertical, when the strip is taller than the window
    view.setScrollBarThickness (6);
    view.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, ui::kDim);
    view.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (view);
    build();
    relayout();
    startTimerHz (30);
}

SFEditor::~SFEditor()
{
    cells.clear();
    setLookAndFeel (nullptr);
}

void SFEditor::build()
{
    for (auto* g : groups) g->cells.clear();
    grid = {};
    cells.clear();                                  // releases a held momentary
    const auto& specs = proc.allSpecs();
    const int firstSys = proc.numModuleParams();
    std::vector<bool> placed (specs.size(), false);

    // The E16: the plugin's page, its own encoders lit where they sit.
    trackSeq = proc.liveTrackNameSeq();
    const std::string mod = proc.mod().id();
    const juce::String liveName = proc.liveTrackName();
    const char* t = e16::trackFor (mod.c_str(), liveName.toRawUTF8());
    page = -1; track = {}; note = {};
    if (t != nullptr && t[0] == 0)
    {
        // On several of the scene's tracks and Live hasn't said which this is (an AU never knows):
        // no grid (its knobs are in PERFORM), a line saying how to get it.
        juce::StringArray tracks;
        for (const auto& k : kE16Knobs) if (mod == k.module) tracks.addIfNotAlreadyThere (k.track);
        note = (proc.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit ? "E16: AN AU KNOWS NO TRACK (" : "E16: NAME THE TRACK (")
             + tracks.joinIntoString (", ") + ")";
    }
    else if (t != nullptr)
    {
        track = t;
        page = e16::homePage (mod.c_str(), t);
        const auto& pg = kE16Pages[page];
        bool swap = false;
        for (int e = 0; e < 16; ++e)
        {
            auto& gc = grid[(size_t) e];
            gc.cell = &pg.cells[e];
            const auto& c = pg.cells[e];
            const bool second = ! e16::turnIs (c.turn, mod.c_str(), t) && e16::turnIs (c.second, mod.c_str(), t);
            const auto* turn = second ? &c.second : e16::turnIs (c.turn, mod.c_str(), t) ? &c.turn : nullptr;
            const int press = e16::pressIs (c.press, mod.c_str(), t) && c.press.param < firstSys ? c.press.param : -1;
            if ((turn == nullptr || turn->param >= firstSys) && press < 0) continue;
            SFCell::Look lk;
            lk.big = lk.e16 = true;
            int param = press;
            if (turn != nullptr && turn->param < firstSys)
            {
                param = turn->param;
                lk.led = ringColour (turn->colour);
                auto* prm = proc.param (param);
                lk.top = turn->max > 0.0f ? juce::jmax (0.01f, prm->convertTo0to1 (turn->max)) : 1.0f;
                lk.press = press;
            }
            cells.push_back (std::make_unique<SFCell> (proc, param, lk));
            gc.mine = cells.back().get();
            body.addAndMakeVisible (*gc.mine);
            placed[(size_t) param] = true;
            if (press >= 0) placed[(size_t) press] = true;
            swap = swap || second;
        }
        // Where else it is: an encoder on every PERF page (NEXT SONG, BUILD) once, others by page.
        juce::StringArray every, also;
        for (int e = 0; e < 16; ++e)
            if (e16::onEveryShowPage (e, mod.c_str(), t, page)) every.add (juce::String (e + 1));
        for (int pg2 = 0; pg2 < e16::kNumPages; ++pg2)
            if (pg2 != page)
                for (int e = 0; e < 16; ++e)
                    if (e16::cellIs (kE16Pages[pg2].cells[e], mod.c_str(), t) && ! (e16::isShowPage (pg2) && every.contains (juce::String (e + 1))))
                        also.add (juce::String (kE16Pages[pg2].name) + " " + juce::String (e + 1));
        const juce::String everyPage = " ON EVERY " + juce::String (kE16ShowScene) + " PAGE";
        if (! every.isEmpty()) also.insert (0, (every.size() == 1 ? "ENCODER " : "ENCODERS ") + every.joinIntoString (", ") + everyPage);
        if (swap) note = "ENCODER 1: PRESS FOR SLOW ALL," + everyPage;
        else if (! also.isEmpty()) note = "ALSO " + also.joinIntoString (", ");
    }

    // The groups, in the order of the strip.
    for (int i = 0; i < (int) specs.size(); ++i)
    {
        const auto& s = specs[(size_t) i];
        if (s.hidden || placed[(size_t) i]) continue;
        Group* gr = i >= firstSys + SFProcessor::SYS_M1_SRC ? &motion
                  : i >= firstSys ? &system
                  : s.primary || s.perform ? &perform
                  : s.inSample ? &sample
                  : s.inEngine ? &engine
                  : s.inLayer ? &layer
                  : s.setup ? &setup : &detail;
        SFCell::Look lk;
        lk.big = gr == &perform;
        lk.narrow = gr == &motion || gr == &system;
        cells.push_back (std::make_unique<SFCell> (proc, i, lk));
        body.addAndMakeVisible (*cells.back());
        gr->cells.push_back (cells.back().get());
    }
    // DETAIL: MODEL first, then the knobs, the menus, the switches (each in the module's order).
    auto rank = [&] (const SFCell* c)
    {
        const auto& s = specs[(size_t) c->index()];
        return s.id == "model" ? 0 : c->getKind() == SFCell::Kind::Ring ? 1 : c->getKind() == SFCell::Kind::Menu ? 2 : 3;
    };
    std::stable_sort (detail.cells.begin(), detail.cells.end(), [&] (const SFCell* a, const SFCell* b) { return rank (a) < rank (b); });
}

// The tallest the window may be: the screen it is on (the main one until it shows), less Live's
// title bar for the plugin window and a margin.
int SFEditor::heightLimit() const
{
    if (testHeightLimit > 0) return testHeightLimit;
    const auto& ds = juce::Desktop::getInstance().getDisplays();
    const auto* d = isShowing() ? ds.getDisplayForRect (getScreenBounds()) : ds.getPrimaryDisplay();
    if (d == nullptr) return 900;
    return std::max (kHeader + 240, d->userArea.getHeight() - 64);
}

void SFEditor::relayout()
{
    uiShown = proc.uiFold.load();
    for (auto* gr : groups) gr->lastSummary = summary (*gr);
    const int content = layout();
    body.setSize (kW, content);
    const int h = std::min (kHeader + content, heightLimit());
    // Taller than the window: the wheel scrolls the strip and turns no knob (a knob under the
    // pointer would take it otherwise). Not: as ever.
    const bool scrolls = kHeader + content > h;
    for (auto& c : cells) c->setWheel (! scrolls);
    if (getWidth() != kW || getHeight() != h) setSize (kW, h);
    else resized();
    repaint();
}

void SFEditor::resized()
{
    view.setBounds (0, kHeader, getWidth(), std::max (0, getHeight() - kHeader));
}

int SFEditor::layout()
{
    const int w = kW - 2 * kMargin;
    const bool load = loadButton.isVisible();
    presetButton.setBounds (kMargin, 28, w - 48 - (load ? 42 : 0), 22);
    storeButton.setBounds (presetButton.getRight() + 4, 28, 44, 22);
    loadButton.setBounds (storeButton.getRight() + 4, 28, 38, 22);

    int y = 0;                                                         // (the body's: under the header)
    panel = {};
    if (page < 0 && note.isNotEmpty()) y += 16;                       // the E16 line
    if (page >= 0)
    {
        // A row with a press (THROW, FREEZE, DROP, ROLL) is a little taller: its name sits under the knob.
        int rowY = y + 30;
        for (int row = 0; row < 4; ++row)
        {
            int h = 70;
            for (int e = row * 4; e < row * 4 + 4; ++e)
                if (auto* m = grid[(size_t) e].mine) h = std::max (h, m->naturalHeight());
            for (int e = row * 4; e < row * 4 + 4; ++e)
            {
                auto& gc = grid[(size_t) e];
                gc.r = { kMargin + (e % 4) * kColW, rowY, kColW, h };
                if (gc.mine != nullptr) gc.mine->setBounds (gc.r);
            }
            rowY += h;
        }
        const int bottom = rowY + (note.isNotEmpty() ? 14 : 2);
        panel = { 4, y + 4, kW - 8, bottom - (y + 4) + 4 };
        y = panel.getBottom() + 6;
    }

    auto place = [&] (Group& gr)
    {
        gr.titleRow = {};
        if (gr.cells.empty()) return;
        gr.titleRow = { 0, y, kW, kTitleH };
        y += kTitleH;
        const bool f = gr.isFolded (uiShown);
        for (auto* c : gr.cells) c->setVisible (! f);
        if (f) { y += 2; return; }
        // Rows of 4 columns in the group's order; a two-column menu that doesn't fit a row's last
        // column leaves it to the next cell that does.
        std::vector<SFCell*> todo (gr.cells.begin(), gr.cells.end());
        int col = 0, rowH = 0;
        while (! todo.empty())
        {
            auto it = todo.begin();
            if (col + std::min (kCols, (*it)->span()) > kCols)
                it = std::find_if (todo.begin(), todo.end(), [&] (SFCell* c) { return col + std::min (kCols, c->span()) <= kCols; });
            if (it == todo.end()) { y += rowH; col = 0; rowH = 0; continue; }
            auto* c = *it;
            todo.erase (it);
            const int sp = std::min (kCols, c->span());
            c->setBounds (kMargin + col * kColW, y, sp * kColW, c->naturalHeight());
            rowH = std::max (rowH, c->naturalHeight());
            col += sp;
            if (col == kCols) { y += rowH; col = 0; rowH = 0; }
        }
        y += rowH + 6;
    };
    place (perform);
    display = {};
    if (const int dh = proc.mod().displayHeight(); dh > 0)
    {
        display = { kMargin, y + 2, w, dh };
        y = display.getBottom() + 8;
    }
    place (detail);
    place (engine);
    place (layer);
    place (sample);
    place (motion);
    place (system);
    place (setup);
    return y + 4;
}

std::vector<std::pair<int, juce::String>> SFEditor::places() const
{
    std::vector<std::pair<int, juce::String>> out;
    for (int e = 0; e < 16; ++e)
        if (auto* c = grid[(size_t) e].mine)
        {
            const bool pressOnly = c->getKind() == SFCell::Kind::Switch;
            out.push_back ({ c->index(), "E16 " + juce::String (e + 1) + (pressOnly ? " PRESS" : "") });
            if (c->pressIndex() >= 0) out.push_back ({ c->pressIndex(), "E16 " + juce::String (e + 1) + " PRESS" });
        }
    for (auto* g : groups)
        for (auto* c : g->cells) out.push_back ({ c->index(), g->title });
    return out;
}

std::vector<juce::Rectangle<int>> SFEditor::titleRows() const
{
    std::vector<juce::Rectangle<int>> out;
    for (auto* g : groups) if (! g->titleRow.isEmpty()) out.push_back (g->titleRow);
    return out;
}

bool SFEditor::folded (int bit) const
{
    for (auto* g : groups) if (g->bit == bit) return g->isFolded (proc.uiFold.load());
    return false;
}

void SFEditor::toggleFold (int bit)
{
    proc.uiFold.fetch_xor (bit);
    relayout();
    // Opened: the group into view, its title first if it doesn't all fit.
    for (auto* g : groups)
        if (g->bit == bit && ! g->isFolded (uiShown) && ! g->cells.empty())
        {
            int bottom = g->titleRow.getBottom();
            for (auto* c : g->cells) bottom = std::max (bottom, c->getBottom());
            const int top = g->titleRow.getY(), vh = view.getHeight(), y0 = view.getViewPositionY();
            if (bottom > y0 + vh || top < y0) view.setViewPosition (0, std::max (0, std::min (top, bottom - vh)));
        }
}

// ---------------------------------------------------------------- painting
void SFEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::kBg);
    paintHeader (g);
}

void SFEditor::paintBody (juce::Graphics& g)
{
    g.fillAll (ui::kBg);
    if (page >= 0) paintE16 (g);
    else if (note.isNotEmpty()) fitted (g, note, { (float) kMargin, 0.0f, (float) (kW - 2 * kMargin), 14.0f }, ui::kFaint, 8.5f, juce::Justification::centredLeft);
    for (auto* gr : groups) if (! gr->titleRow.isEmpty()) paintTitle (g, *gr);
    if (! display.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion (display);
        proc.mod().paint (g, display.toFloat());
        g.restoreState();
    }
}

void SFEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    g.setColour (ui::kText);
    g.drawRect (getLocalBounds().reduced (2), 2);
    fitted (g, proc.mod().fileHint(), getLocalBounds().toFloat().removeFromBottom (24), ui::kText, 11.0f);
}

void SFEditor::paintHeader (juce::Graphics& g)
{
    ui::label (g, "SF " + juce::String (proc.mod().id()), { (float) kMargin, 4.0f, 120.0f, 20.0f }, ui::kText, 13.0f);
    // Song indicator.
    const int s = proc.activeSong();
    const juce::String song = s < 0 ? "SONG --" : "SONG " + juce::String (s + 1).paddedLeft ('0', 2) + (proc.songStored (s) ? "" : " EMPTY");
    fitted (g, song + (proc.recallWaiting() ? " WAIT" : ""), { 124.0f, 4.0f, 96.0f, 20.0f },
            proc.recallWaiting() ? ui::kGreen : ui::kDim, 9.0f, juce::Justification::centredLeft);
    // Output meter. FAULT (red): the safety stage had to reset the module (NaN or runaway).
    const juce::Rectangle<float> m (224.0f, 11.0f, 34.0f, 6.0f);
    ui::levelBar (g, m, proc.outMeter.peak);
    const bool flt = proc.fault.load();
    const bool heldPanic = sf::Bus::get().d().panicHeld.load() != 0 && sf::Bus::get().controlAlive();
    fitted (g, heldPanic ? "PANIC" : flt ? "FAULT" : "OUT", { 261.0f, 7.0f, 32.0f, 14.0f },
            heldPanic || flt || proc.clipped.load() ? ui::kRed : ui::kFaint, 8.5f, juce::Justification::centredLeft);
    g.setColour (ui::kLine);
    g.drawHorizontalLine (kHeader - 2, 0.0f, (float) getWidth());
}

void SFEditor::paintE16 (juce::Graphics& g)
{
    // The unit: a dark panel, its screen showing the page, 16 encoders. This plugin's are lit (its
    // cells draw them); the others are dark, their names faint, so it is clear where the hands are.
    const auto pr = panel.toFloat();
    g.setColour (juce::Colour (0xff0c0c0c));
    g.fillRoundedRectangle (pr, 6.0f);
    g.setColour (juce::Colour (0xff1e1e1e));
    g.drawRoundedRectangle (pr.reduced (0.5f), 6.0f, 1.0f);
    const juce::Rectangle<float> oled (pr.getCentreX() - 62.0f, pr.getY() + 5.0f, 124.0f, 17.0f);
    g.setColour (ui::kBg);
    g.fillRoundedRectangle (oled, 2.0f);
    g.setColour (juce::Colour (0xff262626));
    g.drawRoundedRectangle (oled, 2.0f, 1.0f);
    fitted (g, kE16Pages[page].name, oled, ui::kText, 10.0f, juce::Justification::centred, true);
    fitted (g, "E16 " + juce::String (kE16Pages[page].scene),
            { pr.getX() + 8.0f, oled.getY(), oled.getX() - pr.getX() - 12.0f, oled.getHeight() }, ui::kFaint, 8.5f, juce::Justification::centredLeft, true);
    fitted (g, track, { oled.getRight() + 4.0f, oled.getY(), pr.getRight() - oled.getRight() - 12.0f, oled.getHeight() },
            ui::kDim, 8.5f, juce::Justification::centredRight);
    for (const auto& gc : grid)
    {
        if (gc.mine != nullptr || gc.cell == nullptr) continue;
        const auto r = gc.r.toFloat();
        const auto& t = gc.cell->turn;
        const juce::Point<float> c (r.getCentreX(), r.getY() + 35.0f);
        ui::drawCap (g, c, 10.5f);
        ui::drawLeds (g, c, 19.0f, 2.15f, 0.0f, 0.0f, ui::kLedWhite, ui::kLedDark);
        fitted (g, t.abbr != nullptr ? juce::String (t.abbr) : juce::String(), r.withHeight (SFCell::kNameH), juce::Colour (0xff3a3a3a), 9.0f);
    }
    if (note.isNotEmpty())
        fitted (g, note, { pr.getX() + 6.0f, (float) grid[12].r.getBottom(), pr.getWidth() - 12.0f, 13.0f }, ui::kFaint, 8.5f);
}

void SFEditor::paintTitle (juce::Graphics& g, const Group& gr)
{
    const auto r = gr.titleRow.toFloat();
    const auto font = ui::mono (9.0f, true).withExtraKerningFactor (0.12f);
    const float tw = juce::GlyphArrangement::getStringWidth (font, gr.title);
    g.setColour (ui::kDim);
    g.setFont (font);
    g.drawText (gr.title, juce::Rectangle<float> ((float) kMargin, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
    const float x0 = (float) kMargin + tw + 8.0f, x1 = r.getRight() - (gr.bit != 0 ? 20.0f : (float) kMargin);
    const bool f = gr.isFolded (uiShown);
    if (f)
        fitted (g, gr.lastSummary, { x0, r.getY(), x1 - x0, r.getHeight() }, ui::kFaint, 8.5f, juce::Justification::centredLeft);
    else
    {
        g.setColour (ui::kLine);
        g.drawHorizontalLine ((int) r.getCentreY(), x0, x1);
    }
    if (gr.bit != 0)
    {
        // + opens, - folds.
        const float cx = r.getRight() - 12.0f, cy = r.getCentreY();
        g.setColour (ui::kDim);
        g.drawLine (cx - 3.5f, cy, cx + 3.5f, cy, 1.2f);
        if (f) g.drawLine (cx, cy - 3.5f, cx, cy + 3.5f, 1.2f);
    }
}

juce::String SFEditor::summary (const Group& gr) const
{
    const int firstSys = proc.numModuleParams();
    auto text = [this] (int i) { return proc.param (i)->getCurrentValueAsText(); };
    if (&gr == &motion)
    {
        juce::StringArray parts;
        for (int k = 0; k < SFProcessor::kMotions; ++k)
        {
            const int src = firstSys + SFProcessor::SYS_M1_SRC + 4 * k;
            const juce::String m = "M" + juce::String (k + 1) + " ";
            parts.add (proc.param (src)->getValue() < 0.001f ? m + "OFF" : m + text (src) + ">" + text (src + 2) + " " + text (src + 1));
        }
        return parts.joinIntoString ("  ");
    }
    if (&gr == &system)
    {
        const int src = firstSys + SFProcessor::SYS_MACRO_SRC;
        return "SLOW " + text (firstSys + SFProcessor::SYS_GLIDE) + "  "
             + (proc.param (src)->getValue() < 0.001f ? juce::String ("MACRO OFF") : text (src) + " " + text (src + 1));
    }
    juce::StringArray parts;
    for (auto* c : gr.cells)
        if (auto s = c->summary(); s.isNotEmpty()) parts.add (s);
    return parts.joinIntoString ("  ");
}

// ---------------------------------------------------------------- mouse, files, timer
SFEditor::Group* SFEditor::groupAt (juce::Point<int> p)
{
    for (auto* g : groups) if (g->bit != 0 && g->titleRow.contains (p)) return g;
    return nullptr;
}

void SFEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 26 && e.x > 220) { proc.clipped = false; proc.fault = false; }
}

// The body's clicks (its own coordinates): the module's display, a group's title line.
void SFEditor::bodyMouse (const juce::MouseEvent& e, bool down)
{
    // A drag that began on the display stays the display's when it leaves it (a marker pulled to
    // the end of a sample, a knob pulled past the edge).
    if (down) displayDrag = display.contains (e.getPosition());
    if (displayDrag)
        proc.mod().displayMouse (e.position, display.toFloat(), down);
    if (down)
        if (auto* gr = groupAt (e.getPosition())) toggleFold (gr->bit);
}

void SFEditor::Body::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (ed.groupAt (e.getPosition()) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

bool SFEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! proc.mod().acceptsFiles()) return false;
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void SFEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    juce::StringArray audio;
    for (auto& f : files) if (isAudioFile (f)) audio.add (f);
    proc.mod().loadFiles (audio);
    repaint();
}

void SFEditor::timerCallback()
{
    // Live renamed the track (or named it after the window opened): the grid follows.
    if (proc.liveTrackNameSeq() != trackSeq)
    {
        const auto mod = proc.mod().id();
        const char* t = e16::trackFor (mod.c_str(), proc.liveTrackName().toRawUTF8());
        trackSeq = proc.liveTrackNameSeq();
        if (juce::String (t != nullptr ? t : "") != track || (t == nullptr) != (page < 0)) { build(); relayout(); }
    }
    if (proc.uiFold.load() != uiShown) relayout();          // a state load
    else if (std::max (0, proc.mod().displayHeight()) != display.getHeight()) relayout();   // the display grew or shrank (a sample in or out)
    else if (ticks % 30 == 0 && std::min (kHeader + body.getHeight(), heightLimit()) != getHeight()) relayout();   // another screen
    if (presetButton.getButtonText() != presetText()) presetButton.setButtonText (presetText());
    // LOAD says what a RESAMPLE is doing: waiting for the bar, recording.
    if (loadButton.isVisible())
    {
        const int st = proc.resampleStage();
        const juce::String t = st == 1 ? "WAIT" : st >= 2 ? "REC" : "LOAD";
        if (loadButton.getButtonText() != t) loadButton.setButtonText (t);
    }
    repaint (0, 0, getWidth(), 26);
    if (! display.isEmpty()) body.repaint (display);
    for (auto& c : cells) if (c->isVisible()) c->poll();
    if (++ticks % 6 == 0)                                   // a folded group's line, 5 times a second
        for (auto* gr : groups)
            if (gr->isFolded (uiShown))
                if (auto s = summary (*gr); s != gr->lastSummary) { gr->lastSummary = s; body.repaint (gr->titleRow); }
}

// ---------------------------------------------------------------- presets
juce::String SFEditor::presetText() const
{
    const auto nm = proc.presetName();
    return nm.isEmpty() ? juce::String ("PRESET") : "PRESET  " + nm.fromLastOccurrenceOf ("/", false, false);
}

void SFEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& fac = proc.factoryPresets();
    // "GROUP/NAME" puts a preset in a sub-menu (groups in first-seen order).
    std::map<juce::String, juce::PopupMenu> sub;
    std::vector<juce::String> order;
    juce::PopupMenu loose;
    for (size_t i = 0; i < fac.size(); ++i)
    {
        const juce::String nm (fac[i].name);
        const int slash = nm.indexOfChar ('/');
        const bool current = nm == proc.presetName();
        if (slash > 0)
        {
            const auto grp = nm.substring (0, slash);
            if (sub.find (grp) == sub.end()) order.push_back (grp);
            sub[grp].addItem (1 + (int) i, nm.substring (slash + 1), true, current);
        }
        else loose.addItem (1 + (int) i, nm, true, current);
    }
    if (! fac.empty())
    {
        menu.addSectionHeader ("FACTORY");
        for (auto& grp : order) menu.addSubMenu (grp, sub[grp]);
        for (juce::PopupMenu::MenuItemIterator it (loose); it.next();) menu.addItem (it.getItem());
    }
    userFiles = proc.userPresets();
    if (! userFiles.isEmpty())
    {
        menu.addSectionHeader ("USER");
        for (int i = 0; i < userFiles.size(); ++i) menu.addItem (1000 + i, userFiles[i].getFileNameWithoutExtension());
    }
    // The randomiser: the unlocked controls (right-click a knob to lock or unlock it).
    if (! fac.empty())
    {
        menu.addSeparator();
        menu.addItem (3000, "RANDOMISE THE UNLOCKED");
        menu.addItem (3001, "NUDGE THE UNLOCKED");
        menu.addItem (3002, "LOCKS AS AT FIRST");
        menu.addItem (3003, "UNLOCK ALL");
    }
    menu.addSeparator();
    menu.addItem (2000, "SAVE USER PRESET...");
    menu.addItem (2001, "SHOW PRESET FOLDER");
    // The host can close the editor while the menu is open: the callback must not touch it then.
    juce::Component::SafePointer<SFEditor> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton), [self] (int r)
    {
        if (self == nullptr) return;
        self->presetChosen (r);
    });
}

void SFEditor::presetChosen (int r)
{
    const auto& f = proc.factoryPresets();
    if (r >= 1 && r <= (int) f.size()) proc.applyPreset (f[(size_t) r - 1]);
    else if (r >= 1000 && r < 1000 + userFiles.size()) proc.loadUserPreset (userFiles[r - 1000]);
    else if (r == 2000)
    {
        auto* w = new juce::AlertWindow ("SAVE USER PRESET", "NAME", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.presetName().isEmpty() ? juce::String ("USER 01") : proc.presetName());
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SFEditor> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int res)
        {
            if (res == 1 && safe != nullptr) safe->proc.saveUserPreset (w->getTextEditorContents ("name"));
        }), true);
    }
    else if (r == 2001)
    {
        auto folder = proc.userPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
    else if (r == 3000 || r == 3001) proc.randomise (r == 3001, (uint32_t) juce::Time::getHighResolutionTicks());
    else if (r == 3002 || r == 3003)
    {
        if (r == 3002) proc.defaultLocks();
        else for (int i = 0; i < proc.numModuleParams(); ++i) proc.setLocked (i, false);
        for (auto& c : cells) c->repaint();
    }
    presetButton.setButtonText (presetText());
}

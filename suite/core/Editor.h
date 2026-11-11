#pragma once
// SFEditor: one generic editor for every module, a narrow strip so several sit side by side in Live.
// Top to bottom, from the hands to the set and forget:
//   HEADER   SF <MODULE>, SONG, OUT meter (FAULT / PANIC); PRESET, STORE, LOAD
//   E16      the plugin's page of the scenes PERF and MIX as the E16 shows it: the plugin's own encoders
//            lit in their ring colours where they sit, the other encoders dark (E16View.h). A plugin
//            the scene doesn't have: no grid; one on several tracks Live hasn't named: a line.
//   PERFORM  the rest of the top row (the first 8 parameters: Live's device view), and what else
//            is played (DECK's PLAY and CUE, CONTROL's PANIC)
//   DISPLAY  the module's own panel
//   DETAIL   the sound's other controls, MODEL first
//   SAMPLE   a sampler's START, END, FADE IN, FADE OUT                   folded at first
//   MOTION   M1 / M2 SOURCE, RATE, TARGET, DEPTH                         folded at first
//   SYSTEM   SLOW, MACRO SRC, MACRO AMT                                  folded at first
//   SETUP    how the plugin sits in the set (ALLOC, KEY FOLLOW, DRY ...)  folded at first
// A folded group's title line says what is in it; a click on the line opens or folds it. What is
// folded is saved with the set (SFProcessor::uiFold).
// The window is never taller than the screen it is on allows (heightLimit): below the header, the
// strip scrolls (a thin bar at the right; the wheel scrolls it, and turns no knob, while it does).
// A group opened at the bottom scrolls into view.
#include "Processor.h"
#include "Ui.h"
#include "Sample.h"
#include "E16View.h"

class SFLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SFLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}   // SFCell draws the ring
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return sf::ui::mono (10.0f); }
    juce::Font getPopupMenuFont() override { return sf::ui::mono (12.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

class SFCell;

class SFEditor : public juce::AudioProcessorEditor,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    explicit SFEditor (SFProcessor&);
    ~SFEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override { relayout(); }   // on screen: that screen's height
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }

    // The strip: 4 columns of 70 (the E16's 4 x 4), an 8 pixel margin.
    static constexpr int kW = 296, kMargin = 8, kCols = 4, kColW = 70, kHeader = 56, kTitleH = 16;
    enum Fold { kFoldDetail = 1, kFoldMotion = 2, kFoldSystem = 4, kFoldSetup = 8, kFoldSample = 16, kFoldLayer = 32, kFoldEngine = 64 };

    // What the strip shows, for tests: the E16 page (-1: no grid), the track the grid is for, every
    // shown parameter's place ("E16 <encoder 1-16>", "E16 <encoder> PRESS", or the group's title), and
    // whether a group is folded.
    int e16Page() const { return page; }
    juce::String e16Track() const { return track; }
    std::vector<std::pair<int, juce::String>> places() const;
    bool folded (int foldBit) const;
    void toggleFold (int foldBit);
    juce::Rectangle<int> displayBounds() const { return getLocalArea (&body, display); }
    // The scrolling: the strip below the header (body), its view, and the tallest the window may be.
    // testHeightLimit (tests, pictures): that limit in place of the screen's (0: the screen's).
    static inline int testHeightLimit = 0;
    int heightLimit() const;
    juce::Viewport& scroller() { return view; }
    juce::Component& strip() { return body; }
    int contentHeight() const { return body.getHeight(); }
    std::vector<juce::Rectangle<int>> titleRows() const;     // the groups' title lines (the strip's coordinates)

private:
    // The strip below the header: the E16, the groups and the module's display. It paints and takes
    // the clicks for them (the editor's own code: SFEditor::paintBody and the body* handlers).
    struct Body : juce::Component
    {
        SFEditor& ed;
        explicit Body (SFEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintBody (g); }
        void mouseDown (const juce::MouseEvent& e) override { ed.bodyMouse (e, true); }
        void mouseDrag (const juce::MouseEvent& e) override { ed.bodyMouse (e, false); }
        void mouseUp (const juce::MouseEvent&) override { ed.displayDrag = false; ed.proc.mod().displayMouseUp(); }
        void mouseMove (const juce::MouseEvent& e) override;
    };
    struct Group
    {
        juce::String title;
        int bit = 0;                    // its fold bit (0: never folds)
        bool foldedAtFirst = false;
        std::vector<SFCell*> cells;
        juce::Rectangle<int> titleRow;
        juce::String lastSummary;
        bool isFolded (int ui) const { return bit != 0 && (foldedAtFirst != ((ui & bit) != 0)); }
    };
    struct GridCell
    {
        const sf::E16Cell* cell = nullptr;
        SFCell* mine = nullptr;         // this plugin's encoder, lit
        juce::Rectangle<int> r;
    };

    void timerCallback() override;
    void build();                       // the cells for the track Live names (again when it renames it)
    void relayout();                    // places everything and sizes the window
    int layout();
    void paintHeader (juce::Graphics&);
    void paintBody (juce::Graphics&);
    void bodyMouse (const juce::MouseEvent&, bool down);
    void paintE16 (juce::Graphics&);
    void paintTitle (juce::Graphics&, const Group&);
    juce::String summary (const Group&) const;
    Group* groupAt (juce::Point<int>);
    void showPresetMenu();
    void presetChosen (int result);
    juce::String presetText() const;

    SFProcessor& proc;
    SFLookAndFeel lnf;
    std::vector<std::unique_ptr<SFCell>> cells;
    Group perform, detail, engine, layer, sample, motion, system, setup;
    std::array<Group*, 8> groups { &perform, &detail, &engine, &layer, &sample, &motion, &system, &setup };
    std::array<GridCell, 16> grid {};
    int page = -1, uiShown = 0, ticks = 0;
    juce::String track, note;
    juce::Rectangle<int> panel, display;
    bool displayDrag = false;           // a press on the display: its drag is the display's wherever it goes
    uint32_t trackSeq = 0;
    juce::TextButton storeButton { "STORE" }, loadButton { "LOAD" }, presetButton { "PRESET" };
    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHover = false;
    Body body { *this };
    juce::Viewport view;
};

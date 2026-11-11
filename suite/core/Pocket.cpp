#include "Pocket.h"
#include "Ui.h"

namespace sf
{
void Pocket::paint (juce::Graphics& g, juce::Rectangle<float> r) const
{
    ui::frame (g, r, "POCKET");
    auto in = r.reduced (6.0f, 16.0f).withTrimmedTop (2.0f);
    g.setColour (ui::kGrid);
    g.drawHorizontalLine ((int) in.getBottom(), in.getX(), in.getRight());
    ui::trace (g, in, trace, traceWrite, ui::kText);
    const juce::String src = sourceIsBus.load() ? "SRC BUS" : sourceIsSide.load() ? "SRC SIDECHAIN" : "SRC NONE";
    ui::label (g, src, r.reduced (6, 2).removeFromBottom (14), sourceIsBus.load() || sourceIsSide.load() ? ui::kGreen : ui::kFaint, 10.0f,
               juce::Justification::centredRight);
}
} // namespace sf

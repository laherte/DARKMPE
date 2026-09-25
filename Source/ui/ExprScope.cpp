#include "ExprScope.h"
#include "Theme.h"

ExprScope::ExprScope (DarkMPEProcessor& p) : proc (p)
{
    setOpaque (true);
    timerCallback();
    startTimerHz (12);
}

ExprScope::~ExprScope() { stopTimer(); }

void ExprScope::timerCallback()
{
    std::vector<float> now;
    for (const auto& id : DarkMPEProcessor::expressionParamIds())
        now.push_back (proc.apvts.getRawParameterValue (id)->load());
    now.push_back ((float) proc.getSeed());
    if (now == snapshot)
        return;
    snapshot = std::move (now);
    demo = proc.expressionDemo();
    repaint();
}

void ExprScope::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (bg());

    auto area = getLocalBounds().toFloat().reduced (1.0f);
    auto timbre = area.removeFromBottom (12.0f);
    auto lane = area;
    drawBendGrid (g, lane);

    const double L = std::max (1.0, demo.lengthBeats);
    auto xOf = [&] (double beat) { return lane.getX() + (float) (beat / L) * (lane.getWidth() - 32.0f); };
    const float mid = lane.getCentreY(), half = lane.getHeight() * 0.5f - 1.0f;

    for (const auto& n : demo.notes)
    {
        // the note's extent, as a faint bar on the centre line
        g.setColour (accent().withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (xOf (n.start), mid - 1.5f, std::max (2.0f, xOf (n.end()) - xOf (n.start)), 3.0f));

        if (! n.bend.empty())
        {
            juce::Path path;
            path.startNewSubPath (xOf (n.start + n.bend.front().t), mid - bendAxis (n.bend.front().v) * half);
            for (const auto& pt : n.bend)
                path.lineTo (xOf (n.start + pt.t), mid - bendAxis (pt.v) * half);
            g.setColour (bendCol());
            g.strokePath (path, juce::PathStrokeType (1.4f));
        }
        if (n.slide.size() > 1)
        {
            juce::Path path;
            path.startNewSubPath (xOf (n.start + n.slide.front().t), timbre.getBottom() - n.slide.front().v * timbre.getHeight());
            for (const auto& pt : n.slide)
                path.lineTo (xOf (n.start + pt.t), timbre.getBottom() - pt.v * timbre.getHeight());
            g.setColour (slideCol().withAlpha (0.7f));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }
    }

    g.setColour (textDim());
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("PREVIEW  " + proc.describeExpression(), lane.reduced (4.0f, 1.0f), juce::Justification::topLeft);
    g.setColour (grid().brighter (0.3f));
    g.drawRect (getLocalBounds().toFloat(), 1.0f);
}

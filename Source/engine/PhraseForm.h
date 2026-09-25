#pragma once

#include <string>
#include <vector>

namespace dmpe
{

// Phrase forms: how a basic idea (A) is stated, answered and closed. Classic keeps the generators' own
// development; the others are call-and-response structures from songwriting and classical phrase theory.
enum class Form
{
    classic,
    callResponse, // A B        : call, answer (open)
    abac,         // A B A C    : call, open answer, call again, closing answer
    aaba,         // A A B A    : statement, repeat, contrast, return
    aaab,         // A A A B    : three times, then the twist
    period,       // A B A B'   : antecedent / consequent, the second answer closes on the tonic
    sentence,     // A A' F C   : basic idea, restated a third up, fragmentation, cadence
    sequence,     // A A+ A++ C : the idea climbs a step at a time, then closes
    count
};

inline const char* const formNames[] = { "Classic", "Call & Response", "A B A C", "A A B A", "A A A B",
                                         "Period A B A B'", "Sentence A A' F C", "Sequence A A+ A++ C" };

enum class SectionKind
{
    statement,    // A (shift > 0: A', A+, A++: moved up by scale steps)
    answer,       // B: A's rhythm and opening, mirrored contour, ends open (on the fifth)
    closedAnswer, // B': the answer ending on the tonic
    close,        // C: A's opening, then a stepwise descent to the tonic held long
    fragment      // F: the first half of A, twice, the repeat a step higher
};

struct Section
{
    SectionKind kind = SectionKind::statement;
    int shift = 0; // scale steps (statement only)
    std::string label = "A";
};

// One section per phrase unit (a bar of melody, or a group of chords): the form's 4-unit pattern, repeated.
// Classic gives an empty list.
inline std::vector<Section> formSections (Form f, int units)
{
    using K = SectionKind;
    const Section a { K::statement, 0, "A" }, b { K::answer, 0, "B" }, bClosed { K::closedAnswer, 0, "B'" },
                  c { K::close, 0, "C" }, frag { K::fragment, 0, "F" };

    std::vector<Section> pattern;
    switch (f)
    {
        case Form::callResponse: pattern = { a, b }; break;
        case Form::abac:         pattern = { a, b, a, c }; break;
        case Form::aaba:         pattern = { a, a, b, a }; break;
        case Form::aaab:         pattern = { a, a, a, b }; break;
        case Form::period:       pattern = { a, b, a, bClosed }; break;
        case Form::sentence:     pattern = { a, { K::statement, 2, "A'" }, frag, c }; break;
        case Form::sequence:     pattern = { a, { K::statement, 1, "A+" }, { K::statement, 2, "A++" }, c }; break;
        case Form::classic:
        case Form::count:        return {};
    }

    std::vector<Section> out;
    for (int i = 0; i < units; ++i)
        out.push_back (pattern[(size_t) i % pattern.size()]);
    return out;
}

} // namespace dmpe

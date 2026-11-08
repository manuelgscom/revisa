#pragma once
// Engine that replays an external correction of a whole text (`revisa check --jsonl --replay`):
// the output of a reference system goes through the same path and guards as the product, without
// a model.
//
// At construction: the leading and trailing whitespace of corrected is made equal to the text's
// (the path never touches it), then diff::diff(nfc(text), nfc(corrected)) gives the edits over
// the whole text. Each edit is assigned to the segment (segment::segment over text) that contains
// it; an insertion at a segment's start or end belongs to that segment. An edit contained in no
// segment (it crosses a boundary, or lies between two segments) is dropped and counted, except
// a change of whitespace only between segments, which the path never sees either.
//
// correct(s): s is matched, in order, with the next segment whose NFC text is s; the answer is
// that segment with its edits applied. A segment whose place in nfc(text) does not hold exactly
// its own NFC text (NFC across its boundary) comes back unchanged and its edits are counted; a
// call that matches no segment comes back unchanged and counts 1. Segments the path skips are
// never asked for, so their edits are lost as in the product and not counted.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/diff/diff.hpp"
#include "core/infer/engine.hpp"

namespace revisa::infer {

class ReplayEngine final : public Engine {
public:
    ReplayEngine(std::string_view text, std::string_view corrected);

    EngineResult correct(std::string_view segment) override;

    // Edits dropped so far: crossing a boundary, or of a segment that did not match.
    int dropped() const { return dropped_; }

private:
    struct Seg {
        std::string nfc;                // NFC of the segment alone: what the engine is asked for
        std::size_t start = 0;          // its byte offset in nfc(text)
        bool matched = false;           // nfc(text) holds exactly `nfc` at `start`
        std::vector<diff::Edit> edits;  // edits inside it, offsets relative to start
    };

    std::vector<Seg> segs_;
    std::size_t next_ = 0;  // first segment not yet asked for
    int dropped_ = 0;
};

}  // namespace revisa::infer

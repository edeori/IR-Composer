#pragma once

#include <array>
#include "Parameters.h"

// The blend pad's geometry and mix law, shared by the live audio path, the export and
// the pad UI so all three always agree.
//
// Each slot sits on a fixed corner of a unit square (slot 1 top-left, 2 top-right,
// 3 bottom-left, 4 bottom-right). Fixed rather than re-laid-out per active slot count,
// so an automated/saved puck position always means the same thing. The puck's weight
// for each *active* slot is inverse-squared-distance, normalised to sum to 1: on a
// corner that slot is ~100%, in the centre all active slots share equally, and with
// only two active slots the pad behaves as a plain crossfade between them. Since every
// slot's kernel is level-matched to 0dB on its own (see BlendEngine), weights summing to
// 1 keep the blend's level steady wherever the puck is -- the ratio changes, not the
// loudness.
namespace BlendWeights
{
    struct Point { float x, y; };

    inline Point cornerFor (int slot) noexcept
    {
        return { (slot % 2 == 0) ? 0.0f : 1.0f, (slot < 2) ? 0.0f : 1.0f };
    }

    using Weights = std::array<float, IRComposerConstants::numSlots>;
    using ActiveMask = std::array<bool, IRComposerConstants::numSlots>;

    inline Weights compute (float puckX, float puckY, const ActiveMask& active) noexcept
    {
        Weights w {};
        float total = 0.0f;
        for (int s = 0; s < IRComposerConstants::numSlots; ++s)
        {
            if (! active[(size_t) s])
                continue;
            const auto c = cornerFor (s);
            const auto dx = puckX - c.x, dy = puckY - c.y;
            w[(size_t) s] = 1.0f / (dx * dx + dy * dy + 1.0e-4f);
            total += w[(size_t) s];
        }

        if (total > 0.0f)
            for (auto& v : w)
                v /= total;
        return w;
    }
}

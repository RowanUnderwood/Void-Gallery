#pragma once
// "Make way" for floating mode: cards fly toward the camera (+z) at different cruise speeds, so a
// faster card catches slower ones. When two cards overlap on screen and one is closing in on the
// other, they gently slide apart sideways; if they still meet, they bump (momentum-conserving,
// low restitution) and are kept at least `gap` apart in z. A card that was pushed eases back to
// its own cruise speed. Cards therefore never pass through each other, which removed the visual
// "pop" when the depth order of two overlapping cards flipped.

#include <vector>

#include <glm/glm.hpp>

namespace it {

struct FloatBody {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};       // units / second
    glm::vec2 half{1.0f};      // half extents of the collision footprint (visible pixels) in x / y
    glm::vec2 offset{0.0f};    // footprint centre relative to pos (transparent images)
    float cruise = 10.0f;      // the card's own speed toward the camera (units / second)
};

struct MakeWayParams {
    float radius = 85.0f;        // tunnel/float radius: cards are kept roughly within [0.5, 1.1] of it
    float gap = 2.0f;            // minimum z distance between overlapping cards
    float lookahead = 1.5f;      // seconds of warning before contact for the sideways push
    float push = 60.0f;          // sideways acceleration at full urgency (units / s^2)
    float padding = 4.0f;        // extra footprint margin so cards clear each other visibly
    float restitution = 0.2f;    // bump "bounciness" (gentle)
    float speedRelax = 0.6f;     // 1/s: how fast a pushed card returns to its cruise speed
    float lateralDamping = 0.8f; // 1/s: sideways drift dies out
};

// Advances positions by dt and resolves interactions. Returns the number of contacts this step.
int stepMakeWay(std::vector<FloatBody>& bodies, float dt, const MakeWayParams& p);

}  // namespace it

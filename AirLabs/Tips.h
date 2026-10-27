#pragma once
// Training tips: one at a time on the Play page (Next tip cycles them), all of them on Help.
// The first is the one a new player sees first.

namespace airlabs
{
    struct Tip { const char* title; const char* body; };

    inline constexpr Tip kTips[] = {
        { "Track is the best mode to train.",
          "Aerials are about holding your nose on a moving ball while you fly, not snapping to it once. Track trains exactly "
          "that, and it scores you 120 times a second, so every small correction counts. If you only play one mode, play Track." },
        { "Start slow, then speed up.",
          "Begin on Track Control. Move up to Standard once you're on the ball about 75% of the time, then Fast and Insane. "
          "Stats > Overview shows your tracking ceiling: the fastest preset where you've reached 75%." },
        { "Small, steady stick movements.",
          "Control's ball needs only about 10% stick. Push gently and keep pushing; big inputs overshoot and you spend the "
          "next second coming back." },
        { "Lower your deadzone.",
          "Small corrections only count if the game sees them. In Rocket League's controller settings, lower the deadzone a "
          "little at a time until your stick starts to drift, then go back one step." },
        { "Roll, then pitch.",
          "Your car turns faster with pitch than with yaw. For a big turn, air roll until the ball is above or below your "
          "nose, then pitch onto it. To train pitch and yaw on their own, turn on No air roll." },
        { "Watch the best runs.",
          "Press Watch on the Leaderboard page to see a top run in your game. Look at how smoothly the car moves, and how "
          "early it slows down before it reaches the ball." },
        { "Train your weak side.",
          "Stats > Directions shows the directions you're slowest at. Flick's Weak-spot mode sends more balls there." },
        { "A little every day.",
          "A few runs a day beats one long session. Two Track runs make a good warm-up before you play ranked." },
    };
    inline constexpr int kTipCount = static_cast<int>(sizeof(kTips) / sizeof(kTips[0]));
}

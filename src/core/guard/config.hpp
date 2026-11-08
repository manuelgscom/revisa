#pragma once
// Guard thresholds: the single place where the engine fixes them. Calibrated on the
// development set with revisa-0.8b (never on the test set), with the adoption rule written
// beforehand: mean F0.5 of the parts 81.3 -> 83.4, clean false alarms
// within 3 % in every part. Changing them does not change the model contract.

namespace revisa::guard {

// Guard 2: a segment whose edited code points / segment code points exceeds this stays unchanged.
inline constexpr double kMaxChangeRatio = 0.2;

// Guard 5: a segment with more OTRO edits than this (after guards 3 and 4) stays unchanged.
// 0: any edit outside the catalog leaves the whole segment untouched («ante la duda, no tocar»).
inline constexpr int kMaxOtro = 0;

// The thresholds of guards 2 and 5 as a value: the product uses the defaults above; the guard
// calibration (development set only) sweeps other values through `revisa check --replay`.
struct Thresholds {
    double max_change_ratio = kMaxChangeRatio;
    int max_otro = kMaxOtro;
};

}  // namespace revisa::guard

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

// Second-order state-variable filter in trapezoidal (TPT) form, after
// Andrew Simper's "Solving the continuous SVF equations using trapezoidal
// integration and equivalent currents" (Cytomic, 2013).
//
// It stays stable and click-free while its coefficients are modulated, needs
// no allocation, and its frequency response has a simple closed form (below),
// which the response display and tests use.

namespace irene::svf
{
    enum class Type { lowPass, highPass, bell, lowShelf, highShelf };

    struct Coeffs
    {
        // Prototype: H(s) = m0 + m1 * BP(s) + m2 * LP(s), with
        // LP(s) = 1 / (s^2 + k s + 1), BP(s) = s / (s^2 + k s + 1), s normalised to g.
        double g = 0.0, k = 1.0;
        double a1 = 1.0, a2 = 0.0, a3 = 0.0;
        double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    inline Coeffs makeCoeffs (Type type, double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const auto f = std::clamp (freq, 5.0, 0.49 * sampleRate);
        const auto qq = std::max (q, 0.05);
        const auto A = std::pow (10.0, gainDb / 40.0);
        auto g = std::tan (std::numbers::pi * f / sampleRate);

        Coeffs c;
        c.k = 1.0 / qq;

        switch (type)
        {
            case Type::lowPass:   c.m0 = 0.0; c.m1 = 0.0;  c.m2 = 1.0;  break;
            case Type::highPass:  c.m0 = 1.0; c.m1 = -c.k; c.m2 = -1.0; break;

            case Type::bell:
                c.k = 1.0 / (qq * A);
                c.m0 = 1.0; c.m1 = c.k * (A * A - 1.0); c.m2 = 0.0;
                break;

            case Type::lowShelf:
                g /= std::sqrt (A);
                c.m0 = 1.0; c.m1 = c.k * (A - 1.0); c.m2 = A * A - 1.0;
                break;

            case Type::highShelf:
                g *= std::sqrt (A);
                c.m0 = A * A; c.m1 = c.k * (1.0 - A) * A; c.m2 = 1.0 - A * A;
                break;
        }

        c.g = g;
        c.a1 = 1.0 / (1.0 + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    /** Complex frequency response at angular frequency omega (radians/sample). */
    inline std::complex<double> response (const Coeffs& c, double omega) noexcept
    {
        // The trapezoidal SVF is the bilinear transform of the analogue prototype.
        const std::complex<double> s { 0.0, std::tan (std::min (omega, std::numbers::pi * 0.999999) * 0.5) / c.g };
        const auto denom = s * s + c.k * s + 1.0;
        return c.m0 + (c.m1 * s + c.m2) / denom;
    }

    struct State
    {
        double ic1 = 0.0, ic2 = 0.0;

        void reset() noexcept { ic1 = ic2 = 0.0; }

        inline double tick (const Coeffs& c, double v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = c.a1 * ic1 + c.a2 * v3;
            const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
        }
    };
}

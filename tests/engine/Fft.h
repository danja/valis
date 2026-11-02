// tests/engine/Fft.h
//
// A radix-2 FFT for tests that look at a spectrum. Header only; `a.size()` must be a power of two.

#pragma once

#include <complex>
#include <numbers>
#include <utility>
#include <vector>

namespace valis::test {

inline void fft(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar(1.0, -2.0 * std::numbers::pi / static_cast<double>(len));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn = 1.0;
            for (std::size_t k = 0; k < len / 2; ++k, wn *= w)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

}  // namespace valis::test

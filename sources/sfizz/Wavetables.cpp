// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "Wavetables.h"
#include "absl/meta/type_traits.h"
#include <kiss_fftr.h>

namespace sfz {

template <class F>
static F clamp(F x, F lo, F hi)
{
    return std::max(lo, std::min(hi, x));
}

//------------------------------------------------------------------------------
void HarmonicProfile::generate(
    nonstd::span<float> table, double amplitude, double cutoff) const
{
    size_t size = table.size();

    typedef std::complex<kiss_fft_scalar> cpx;

    // allocate a spectrum of size N/2+1
    // bins are equispaced in frequency, with index N/2 being nyquist
    std::unique_ptr<cpx[]> spec(new cpx[size / 2 + 1]());

    kiss_fftr_cfg cfg = kiss_fftr_alloc(size, true, nullptr, nullptr);
    if (!cfg)
        throw std::bad_alloc();

    // bins need scaling and phase offset; this IFFT is a sum of cosines
    const std::complex<double> k = std::polar(amplitude * 0.5, M_PI / 2);

    // start filling at bin index 1; 1 is fundamental, 0 is DC
    for (size_t index = 1; index < size / 2 + 1; ++index) {
        if (index * (1.0 / size) > cutoff)
            break;

        std::complex<double> harmonic = getHarmonic(index);
        spec[index] = k * harmonic;
    }

    kiss_fftri(cfg, reinterpret_cast<kiss_fft_cpx*>(spec.get()), table.data());
    kiss_fftr_free(cfg);
}

//------------------------------------------------------------------------------
constexpr unsigned MipmapRange::N;
constexpr float MipmapRange::F1;
constexpr float MipmapRange::FN;

const float MipmapRange::K = 1.0 / F1;
const float MipmapRange::LogB = std::log(FN / F1) / (N - 1);

const std::array<float, 1024> MipmapRange::FrequencyToIndex = []()
{
    std::array<float, 1024> table;

    for (unsigned i = 0; i < table.size() - 1; ++i) {
        float r = i * (1.0f / (table.size() - 1));
        float f = F1 + r * (FN - F1);
        table[i] = getExactIndexForFrequency(f);
    }
    // ensure the last element to be exact
    table[table.size() - 1] = N - 1;

    return table;
}();

float MipmapRange::getIndexForFrequency(float f)
{
    static constexpr unsigned tableSize = FrequencyToIndex.size();

    float pos = (f - F1) * ((tableSize - 1) / static_cast<float>(FN - F1));
    pos = clamp<float>(pos, 0, tableSize - 1);

    int index1 = static_cast<int>(pos);
    int index2 = std::min<int>(index1 + 1, tableSize - 1);
    float frac = pos - index1;

    return (1.0f - frac) * FrequencyToIndex[index1] +
        frac * FrequencyToIndex[index2];
}

float MipmapRange::getExactIndexForFrequency(float f)
{
    float t = (f < F1) ? 0.0f : (std::log(K * f) / LogB);
    return clamp<float>(t, 0, N - 1);
}

const std::array<float, MipmapRange::N + 1> MipmapRange::IndexToStartFrequency = []()
{
    std::array<float, N + 1> table;
    for (unsigned t = 0; t < N; ++t)
        table[t] = std::exp(t * LogB) / K;
    // end value for final table
    table[N] = 22050.0;

    return table;
}();

MipmapRange MipmapRange::getRangeForIndex(int o)
{
    o = clamp<int>(o, 0, N - 1);

    MipmapRange range;
    range.minFrequency = IndexToStartFrequency[o];
    range.maxFrequency = IndexToStartFrequency[o + 1];

    return range;
}

MipmapRange MipmapRange::getRangeForFrequency(float f)
{
    int index = static_cast<int>(getIndexForFrequency(f));
    return getRangeForIndex(index);
}

//------------------------------------------------------------------------------
constexpr unsigned WavetableMulti::_tableExtra;

WavetableMulti WavetableMulti::createForHarmonicProfile(
    const HarmonicProfile& hp, double amplitude, unsigned tableSize, double refSampleRate)
{
    WavetableMulti wm;
    constexpr unsigned numTables = WavetableMulti::numTables();

    wm.allocateStorage(tableSize);

    for (unsigned m = 0; m < numTables; ++m) {
        MipmapRange range = MipmapRange::getRangeForIndex(m);

        double freq = range.maxFrequency;

        // A spectrum S of fundamental F has: S[1]=F and S[N/2]=Fs'/2
        // which lets it generate frequency up to Fs'/2=F*N/2.
        // Therefore it's desired to cut harmonics at C=0.5*Fs/Fs'=0.5*Fs/(F*N).
        double cutoff = (0.5 * refSampleRate / tableSize) / freq;

        float* ptr = const_cast<float*>(wm.getTablePointer(m));
        nonstd::span<float> table(ptr, tableSize);

        hp.generate(table, amplitude, cutoff);
    }

    wm.fillExtra();

    return wm;
}

void WavetableMulti::allocateStorage(unsigned tableSize)
{
    _multiData.resize((tableSize + 2 * _tableExtra) * numTables());
    _tableSize = tableSize;
}

void WavetableMulti::fillExtra()
{
    unsigned tableSize = _tableSize;
    constexpr unsigned tableExtra = _tableExtra;
    constexpr unsigned numTables = WavetableMulti::numTables();

    for (unsigned m = 0; m < numTables; ++m) {
        float* beg = const_cast<float*>(getTablePointer(m));
        float* end = beg + tableSize;
        // fill right
        float* src = beg;
        float* dst = end;
        for (unsigned i = 0; i < tableExtra; ++i) {
            *dst++ = *src;
            src = (src + 1 != end) ? (src + 1) : beg;
        }
        // fill left
        src = end - 1;
        dst = beg - 1;
        for (unsigned i = 0; i < tableExtra; ++i) {
            *dst-- = *src;
            src = (src != beg) ? (src - 1) : (end - 1);
        }
    }
}

//------------------------------------------------------------------------------

/**
 * @brief Harmonic profile which takes its values from a table.
 */
class TabulatedHarmonicProfile : public HarmonicProfile {
public:
    explicit TabulatedHarmonicProfile(nonstd::span<const std::complex<float>> harmonics)
        : _harmonics(harmonics)
    {
    }

    std::complex<double> getHarmonic(size_t index) const override
    {
        if (index >= _harmonics.size())
            return {};

        return _harmonics[index];
    }

private:
    nonstd::span<const std::complex<float>> _harmonics;
};

//------------------------------------------------------------------------------
WavetableMulti WavetableMulti::createFromAudioData(
    nonstd::span<const float> audioData, double amplitude, unsigned tableSize, double refSampleRate)
{
    size_t fftSize = audioData.size();
    size_t specSize = fftSize / 2 + 1;

    typedef std::complex<kiss_fft_scalar> cpx;
    std::unique_ptr<cpx[]> spec { new cpx[specSize] };

    kiss_fftr_cfg cfg = kiss_fftr_alloc(fftSize, false, nullptr, nullptr);
    if (!cfg)
        throw std::bad_alloc();

    kiss_fftr(cfg, audioData.data(), reinterpret_cast<kiss_fft_cpx*>(spec.get()));
    kiss_fftr_free(cfg);

    // scale transform, and normalize amplitude and phase
    const std::complex<double> k = std::polar(2.0 / fftSize, -M_PI / 2);
    for (size_t i = 0; i < specSize; ++i)
        spec[i] *= k;

    TabulatedHarmonicProfile hp {
        nonstd::span<const std::complex<float>> { spec.get(), specSize }
    };

    return WavetableMulti::createForHarmonicProfile(hp, amplitude, tableSize, refSampleRate);
}

} // namespace sfz

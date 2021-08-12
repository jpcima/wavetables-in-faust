// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once
#include <nonstd/span.hpp>
#include <array>
#include <vector>
#include <memory>
#include <complex>

namespace sfz {

/**
   A description of the harmonics of a particular wave form
 */
class HarmonicProfile {
public:
    virtual ~HarmonicProfile() {}

    /**
       @brief Get the value at the given index of the frequency spectrum.

       The modulus and the argument of the complex number are equal to the
       amplitude and the phase of the harmonic component.
     */
    virtual std::complex<double> getHarmonic(size_t index) const = 0;

    /**
       @brief Generate a period of the waveform and store it in the table.

       Do not generate harmonics above cutoff, which is expressed as Fc/Fs.
     */
    void generate(nonstd::span<float> table, double amplitude, double cutoff) const;
};

/**
   A helper to select ranges of a mip-mapped wave, according to the
   frequency of an oscillator.

   The ranges are identified by octave numbers; not octaves in a musical sense,
   but as logarithmic divisions of the frequency range.
 */
class MipmapRange {
public:
    float minFrequency = 0;
    float maxFrequency = 0;

    // number of tables in the mipmap
    static constexpr unsigned N = 24;
    // start frequency of the first table in the mipmap
    static constexpr float F1 = 20.0;
    // start frequency of the last table in the mipmap
    static constexpr float FN = 12000.0;

    static float getIndexForFrequency(float f);
    static float getExactIndexForFrequency(float f);
    static MipmapRange getRangeForIndex(int o);
    static MipmapRange getRangeForFrequency(float f);

    // the frequency mapping of the mipmap is defined by formula:
    //     T(f) = log(k*f)/log(b)
    // - T is the table number, converted to index by rounding down
    // - f is the oscillation frequency
    // - k and b are adjustment parameters according to constant parameters
    //     k = 1/F1
    //     b = exp(log(FN/F1)/(N-1))

    static const float K;
    static const float LogB;

    static const std::array<float, 1024> FrequencyToIndex;
    static const std::array<float, N + 1> IndexToStartFrequency;
};

/**
   Multisample of a wavetable, which is a collection of FFT-filtered mipmaps
   adapted for various playback frequencies.
 */
class WavetableMulti {
public:
    // number of elements in each table
    unsigned tableSize() const { return _tableSize; }

    // number of tables in the multisample
    static constexpr unsigned numTables() { return MipmapRange::N; }

    // get the N-th table in the multisample
    nonstd::span<const float> getTable(unsigned index) const
    {
        return { getTablePointer(index), _tableSize };
    }

    // get the table which is adequate for a given playback frequency
    nonstd::span<const float> getTableForFrequency(float freq) const
    {
        return getTable(MipmapRange::getIndexForFrequency(freq));
    }

    // create a multisample according to a given harmonic profile
    // the reference sample rate is the minimum value accepted by the DSP
    // system (most defavorable wrt. aliasing)
    static WavetableMulti createForHarmonicProfile(
        const HarmonicProfile& hp, double amplitude,
        unsigned tableSize = 2048,
        double refSampleRate = 44100);

    static WavetableMulti createFromAudioData(
        nonstd::span<const float> audioData, double amplitude,
        unsigned tableSize = 2048,
        double refSampleRate = 44100);

private:
    // get a pointer to the beginning of the N-th table
    const float* getTablePointer(unsigned index) const
    {
        return _multiData.data() + index * (_tableSize + 2 * _tableExtra) + _tableExtra;
    }

    // allocate the internal data for tables of the given size
    void allocateStorage(unsigned tableSize);

    // fill extra data at table ends with repetitions of the first samples
    void fillExtra();

    // length of each individual table of the multisample
    unsigned _tableSize = 0;

    // number X of extra elements, for safe interpolations up to X-th order.
    static constexpr unsigned _tableExtra = 4;

    // internal storage, having `multiSize` rows and `tableSize` columns.
    std::vector<float> _multiData;
};

} // namespace sfz

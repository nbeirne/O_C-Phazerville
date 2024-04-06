// Copyright (c) 2018, Jason Justian
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// https://github.com/hollance/synth-recipes/blob/main/recipes/white-noise.markdown

#include <util/util_misc.h>
#include <math.h>
#include <algorithm>
// #include <util/util_macros.h>

// template<typename _Tp>
// _Tp constrain(_Tp value, _Tp min, _Tp max) {
//     return (value < min) ? min : (value > max ? max : value);
// }

#define INPUT_STEP_CONSTRAIN(value, direction, step, min, max) \
    do                                                         \
    {                                                          \
        typeof(value) v = value + (direction * step);          \
        if (v < (min))                                         \
            value = min;                                       \
        else if (v > (max))                                    \
            value = max;                                       \
        else                                                   \
            value = v;                                         \
    } while (0)

#define CONSTRAINV(value, min, max) (value < min) ? min : (value > max ? max : value)

template <typename randomtype>
class RandomGenerator
{
public:
    virtual randomtype next();
    virtual randomtype max();
    virtual uint8_t bits();
};

struct LFSRNoise : RandomGenerator<uint32_t>
{
public:
    LFSRNoise(uint32_t seed = 0x55555555) : seed(seed) {}
    uint32_t seed;

    uint32_t next()
    {
        if (seed & 1)
        {
            seed = (seed >> 1) ^ 0x80000062;
        }
        else
        {
            seed >>= 1;
        }
        return seed;
    }

    uint32_t max()
    {
        return __UINT32_MAX__;
    }

    uint8_t bits()
    {
        return 32;
    }
};

struct LCGNoise
{
    LCGNoise(uint32_t seed = 22222) : seed(seed) {}

    uint32_t next()
    {
        seed = seed * 196314165 + 907633515;
        return seed;
    }

    uint32_t max()
    {
        return 4294967295;
    }

    uint8_t bits()
    {
        return 32;
    }

private:
    uint32_t seed;
};

struct XorShift32Noise
{
    XorShift32Noise() {}
    XorShift32Noise(uint32_t seed) : seed(seed) {}

    uint32_t operator()()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    }

    uint32_t max()
    {
        return __UINT32_MAX__;
    }

    uint8_t bits()
    {
        return 32; // number of bits.
    }

private:
    uint64_t seed = 161803398;
};

struct XorShift64Noise
{
    XorShift64Noise() {}
    XorShift64Noise(uint64_t seed) : seed(seed) {}

    uint64_t operator()()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    }

    uint64_t max()
    {
        return __UINT64_MAX__;
    }

    uint8_t bits()
    {
        return 64; // number of bits.
    }

private:
    uint64_t seed = 161803398;
};

struct FilteredNoise
{
    uint32_t slope; // speed by which the value changes

    FilteredNoise()
    {
        target = random.next();
        direction = 1;
        value = 0;
        slope = ((float)5 / (float)__UINT8_MAX__) * (float)__UINT32_MAX__; // this devolves into a square wave?????
    }

    void setCutoff(int32_t frequency, uint32_t sampleRate)
    {
        // slope = 3.0f * frequency / sampleRate;
        slope = frequency;
    }

    uint32_t operator()()
    {
        // clamp if the value will go out of bounds, otherwise apply slope
        if (direction > 0 && value > max() - slope)
        {
            value = max();
        }
        else if (direction < 0 && value < slope)
        {
            value = 0;
        }
        else
        {
            value += direction * slope;
        }

        // Time to reverse direction?
        if ((direction > 0 && value >= target) || (direction < 0 && value <= target))
        {
            value = target;
            direction = -direction;
            uint32_t rng = random.next();
            if (direction > 0)
            {
                target = ((rng >> 1) | (max() / 2 + 1)); // we want a number in the higher half
            }
            else
            {
                target = rng >> 1; // we want a number in the lower half
            }
        }

        return value;
    }

    uint32_t max()
    {
        return __UINT32_MAX__;
    }

    uint8_t bits()
    {
        return 32;
    }

private:
    LCGNoise random{12345};
    // LFSRNoise random { 12345 };
    // XorShiftNoise random { 12345 };

    uint32_t value;    // current value
    uint32_t target;   // destination value (always positive)
    int32_t direction; // going up (1.0) or down (-1.0)
};

struct AverageBufferFilter
{
    uint8_t max_size = 128;
    uint8_t size;

    AverageBufferFilter(uint8_t size = 64)
    {
        this->size = std::max((uint8_t)1, std::min(max_size, size));
    }

    uint32_t operator()(uint32_t value)
    {
        buffer[index] = value; // post-increment. use buffer[0]
        index = (index + 1) % size;
        return buffer_average();
    }

    uint32_t buffer_average()
    {
        uint32_t average = 0;
        for (uint8_t i = 0; i < size; i++)
        {
            average += buffer[i] / size;
        }
        return average;
    }

private:
    uint8_t index;
    uint32_t buffer[128];
};

class InfiniteImpulseFilter
{
public:
    double coefficient = 0.9; // between 0 and 1
    double last_value = 0;    // y[n-1]

    uint32_t operator()(uint32_t value)
    {
        // out[n] = c*in[n] + (1-c)*out[n-1]
        last_value = (coefficient * (double)value) + ((1.0 - coefficient) * last_value);
        return last_value;
    }
};

class LadderFilter
{
public:
    double resonance = 0.5;
    double coefficient = 0.9;

    uint32_t out1;
    uint32_t out2;
    uint32_t out3;
    uint32_t out4;

    uint32_t operator()(uint32_t v0)
    {
        v0 = (uint32_t)((double)v0 - (((double)out4) * resonance)); // apply resonance

        out1 = (coefficient * (double)v0) + ((1.0 - coefficient) * (double)out1);
        out2 = (coefficient * (double)out1) + ((1.0 - coefficient) * (double)out2);
        out3 = (coefficient * (double)out2) + ((1.0 - coefficient) * (double)out3);
        out4 = (coefficient * (double)out3) + ((1.0 - coefficient) * (double)out4);

        return out4;
    }
};

class ConvoluationFilter
{
    uint8_t size = 128;
    uint8_t index = 0;

    uint32_t buffer[128];
    uint32_t coefficients[128];

    uint32_t operator()(uint32_t value)
    {
        buffer[index] = value; // post-increment. use buffer[0]
        index = (index + 1) % size;
        return convolve();
    }

    uint32_t convolve()
    {
        uint32_t average = 0;
        for (uint8_t i = 0; i < size; i++)
        {
            average += buffer[i] / size;
        }
        return average;
    }
};

class Noise : public HemisphereApplet
{
public:
    const char *applet_name()
    { // Maximum 10 characters
        return "Noise";
    }

    void Start()
    {
    }

    int scale(uint32_t value, uint32_t value_range, uint8_t bits, int32_t maxrange)
    {
        float scaled = ((float)value / (float)value_range) * (float)maxrange;
        return scaled;
    }

    void Controller()
    {
        int maxrange = 7680; // 3v=4608 5v=7680

        // noise channel
        uint32_t noise = lcg_noise.next();
        Out(0, scale(noise, filt_noise.max(), lcg_noise.bits(), maxrange)); // 3v=4608 5v=7680

        // filter channel
        uint32_t f = avg_filt(noise);

        // uint32_t f = xorshift_noise();

        // uint32_t f  = avg_filt(noise);
        Out(1, scale(f, xorshift_noise.max(), xorshift_noise.bits(), maxrange)); // 3v=4608 5v=7680

        // TODO: how to filter (get brown, pink, etc)
        // TODO: noise plethora https://www.befaco.org/noise-plethora/
        // cutoff
        // resonance
        // bounds checking in abuf_filt (for size)
        // perlin noise generator
        // pink noise fitler
        // FIR filter
        // infinite impulse filter
        // ladder filter
        // Z-domain
        // iif + feedback

        // average filter emphasizes harmonics based on size. Interesting.

        count++;
    }

    void View()
    {
        // DrawIndicator();
    }

    int mode = 0;
    void OnButtonPress()
    {
        mode = (mode + 1) % 2;
    }

    void OnEncoderMove(int direction)
    {
        // we want to max out a UINT32_MAX / 4... maybe...
        float slope = direction;

        filt_noise.slope += ((float)slope / (float)__UINT8_MAX__) * (float)__UINT32_MAX__;

        INPUT_STEP_CONSTRAIN(avg_filt.size, direction, 1, 1, avg_filt.max_size);
        INPUT_STEP_CONSTRAIN(inf_imp_filt.coefficient, direction, 0.01, 0.01, 0.99);

        if (mode == 0)
        {
            INPUT_STEP_CONSTRAIN(ladder_filt.coefficient, direction, 0.01, 0.01, 0.99);
        }
        else if (mode == 1)
        {
            INPUT_STEP_CONSTRAIN(ladder_filt.resonance, direction, 0.00, 0.01, 0.99);
        }
    }

    uint64_t OnDataRequest()
    {
        uint64_t data = 0;
        // Pack(data, PackLocation {0,8}, attack);
        // Pack(data, PackLocation {8,8}, decay);
        return data;
    }

    void OnDataReceive(uint64_t data)
    {
        // attack = Unpack(data, PackLocation {0,8});
        // decay = Unpack(data, PackLocation {8,8});
    }

protected:
    void SetHelp()
    {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "";
        help[HEMISPHERE_HELP_CVS] = "0";
        help[HEMISPHERE_HELP_OUTS] = "0";
        help[HEMISPHERE_HELP_ENCODER] = "0";
        //                               "------------------" <-- Size Guide
    }

private:
    // lfsr_noise + lcg_noise
    // lcg_noise + filt_noise
    // avg_filt
    // lcg_noise + inf_imp_filt
    LFSRNoise lfsr_noise{12345};
    XorShift32Noise xorshift_noise{12345};
    LCGNoise lcg_noise{12345};
    FilteredNoise filt_noise;

    AverageBufferFilter avg_filt;
    InfiniteImpulseFilter inf_imp_filt;
    ConvoluationFilter convolution_filt;
    LadderFilter ladder_filt;

    int32_t encoderVal = 0;
    int count = 0;
};

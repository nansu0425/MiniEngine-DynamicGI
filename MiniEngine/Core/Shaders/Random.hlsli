#ifndef __RANDOM_HLSLI__
#define __RANDOM_HLSLI__

// Capsaicin의 src/core/src/components/random_number_generator/random_number_generator.hlsl에서 Random을 가져왔다.
// license는 THIRD_PARTY_NOTICES.md에 있다.
// 고친 점: CPU가 채운 seed buffer를 읽는 MakeRandom 대신, seed와 pixel과 sample 번호를 PCG hash로 섞어 state 하나를 만드는 MakeRandom을 쓴다.
// 그래서 같은 seed, pixel, sample 번호는 frame이나 실행이 달라도 같은 수열을 낸다. 쓰지 않는 randInt(max)와 rand3은 가져오지 않았다.
class Random
{
    uint rngState;

    /**
     * Generate a random uint.
     * @return The new random number (range  [0, 2^32)).
     */
    uint randInt()
    {
        // Using PCG hash function
        uint state = rngState;
        rngState = rngState * 747796405u + 2891336453u;
        uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        return word;
    }

    /**
     * Generate a random number.
     * @return The new random number (range [0->1.0)).
     */
    float rand()
    {
        // Note: Use the upper 24 bits to avoid a bias due to floating point rounding error.
        float ret = (float)(randInt() >> 8) * 0x1.0p-24f;
        return ret;
    }

    /**
     * Generate 2 random numbers.
     * @return The new numbers (range [0->1.0)).
     */
    float2 rand2()
    {
        return float2(rand(), rand());
    }
};

// Random::randInt가 state를 한 걸음 옮기고 내는 값으로 value를 섞는다.
uint PcgHash(uint value)
{
    Random random;
    random.rngState = value * 747796405u + 2891336453u;
    return random.randInt();
}

// sample 하나가 쓰는 수열. seed가 다르면 같은 pixel과 sample 번호에서도 다른 수열이 된다.
Random MakeRandom(uint seed, uint2 pixel, uint sampleIndex)
{
    Random random;
    random.rngState = PcgHash(seed + PcgHash(pixel.x + PcgHash(pixel.y + PcgHash(sampleIndex))));
    return random;
}

#endif

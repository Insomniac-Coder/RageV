#pragma once
#include <cmath>

namespace RageV
{
    float noise1(int x, int y)
    {
        int n = x + y * 57;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    }

    float smoothnoise(float x, float y)
    {
        float corners = (noise1(x - 1, y - 1) + noise1(x + 1, y - 1) + noise1(x - 1, y + 1) + noise1(x + 1, y + 1)) / 16.0f;
        float sides = (noise1(x - 1, y) + noise1(x + 1, y) + noise1(x, y - 1) + noise1(x, y + 1)) / 8.0f;
        float center = noise1(x, y) / 4.0f;
        return corners + sides + sides;
    }

    float interpolate(float x, float y, float factor)
    {
        return (x * factor) + (y * (factor - 1));
    }

    float interpolatednoise(float x, float y)
    {
        int intpart_x = int(x);
        float decimalpart_x = x - intpart_x;

        int intpart_y = int(y);
        float decimalpart_y = y - intpart_y;

        float v1 = smoothnoise(intpart_x, intpart_y);
        float v2 = smoothnoise(intpart_x + 1, intpart_y);
        float v3 = smoothnoise(intpart_x, intpart_y + 1);
        float v4 = smoothnoise(intpart_x + 1, intpart_y + 1);

        float i1 = interpolate(v1, v2, decimalpart_x);
        float i2 = interpolate(v3, v4, decimalpart_y);

        return interpolate(i1, i2, decimalpart_y);
    }

    float perlin2D(float x, float y)
    {
        float total = 0;
        float persistence = 0.5;
        int n = 7;

        for (int i = 0; i < n; i++)
        {
            int frequency = pow(2, i);
            float amplitude = pow(persistence, i);

            total += interpolatednoise(x * frequency, y * frequency) * amplitude;
        }
        return (total + 1.0f) / 2.0f;
    }
}
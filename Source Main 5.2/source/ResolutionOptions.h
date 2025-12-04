#pragma once

#include <array>

struct ResolutionOption
{
    int width;
    int height;
};

inline constexpr std::array<ResolutionOption, 11> g_ResolutionOptions = {
    ResolutionOption{ 640, 480 },
    ResolutionOption{ 800, 600 },
    ResolutionOption{ 1024, 768 },
    ResolutionOption{ 1280, 1024 },
    ResolutionOption{ 1600, 1200 },
    ResolutionOption{ 1864, 1400 },
    ResolutionOption{ 1600, 900 },
    ResolutionOption{ 1600, 1280 },
    ResolutionOption{ 1680, 1050 },
    ResolutionOption{ 1920, 1080 },
    ResolutionOption{ 2560, 1440 },
};

inline constexpr ResolutionOption GetResolutionOption(int index)
{
    if (index < 0 || index >= static_cast<int>(g_ResolutionOptions.size()))
    {
        return g_ResolutionOptions.front();
    }

    return g_ResolutionOptions[index];
}

inline constexpr int NormalizeResolutionIndex(int index)
{
    const auto count = static_cast<int>(g_ResolutionOptions.size());
    if (count == 0)
    {
        return 0;
    }

    if (index < 0)
    {
        index = count + (index % count);
    }

    if (index >= count)
    {
        index %= count;
    }

    return index;
}

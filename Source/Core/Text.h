#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
    /** "Texto con acentos"_u8 -> juce::String decoded as UTF-8 (juce::String (const char*) expects ASCII). */
    inline juce::String operator""_u8 (const char* text, size_t length)
    {
        return juce::String::fromUTF8 (text, (int) length);
    }
}

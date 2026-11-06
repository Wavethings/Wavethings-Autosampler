#include "Exporter.h"
#include "BinaryData.h"

namespace wt::Templates
{
    juce::String get (const juce::String& originalFileName)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            if (originalFileName == BinaryData::originalFilenames[i])
            {
                int size = 0;
                if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                    return juce::String::fromUTF8 (data, size);
            }
        }

        jassertfalse;
        return {};
    }
}

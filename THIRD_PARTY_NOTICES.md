# Third-party notices

## JUCE
Wavethings Autosampler is built with JUCE 8 (https://github.com/juce-framework/JUCE),
used under the GNU Affero General Public License v3. JUCE is downloaded at build time.

## ConvertWithMoss
Copyright (c) Jürgen Moßgraber — https://github.com/git-moss/ConvertWithMoss
Licensed under the GNU Lesser General Public License v3.0.

The following files are taken unmodified from ConvertWithMoss
(`src/main/resources/de/mossgrabers/convertwithmoss/templates`):

- `Resources/templates/adv/ADV11-Template.xml`
- `Resources/templates/adv/ADV-MultiSamplePart-Template.xml`
- `Resources/templates/nki/Kontakt1_*.xml`

The binary layouts used in `Source/Export/ExsExporter.cpp`, `KontaktExporter.cpp` and the
element structure in `MpcExporter.cpp` follow the corresponding ConvertWithMoss writers.

The optional export formats marked "via ConvertWithMoss" are produced by running a separately
installed copy of ConvertWithMoss; it is not bundled with this application.

## VST3 SDK
The VST3 hosting code is part of JUCE (it includes the parts of the Steinberg VST3 SDK needed for hosting).
VST is a registered trademark of Steinberg Media Technologies GmbH.

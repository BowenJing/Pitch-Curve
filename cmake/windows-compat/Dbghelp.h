#pragma once

// The Windows SDK is case-insensitive on its native filesystem. xwin preserves
// canonical casing on Linux, while JUCE includes this header with a different
// case. Keep this forwarding header isolated to the cross-build include path.
#include <dbghelp.h>

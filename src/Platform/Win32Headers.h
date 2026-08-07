#pragma once

#include "targetver.h"

// These macros configure a vendor/platform header and are the intentional
// exception to the project's constexpr-over-macro rule. They must be defined
// before windows.h is parsed and are not application constants.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>


#pragma once

// Require Windows Vista or later to access newer process APIs
// e.g., QueryFullProcessImageName and PROCESS_QUERY_LIMITED_INFORMATION
#ifndef WINVER
#define WINVER          0x0600
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT    0x0600
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS  0x0600
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION   0x06000000
#endif

#include <windows.h>

#pragma once

#ifdef _WIN32
#include "Windows/RetSpoofInvoker.h"
using RetSpoofInvoker = windows_platform::RetSpoofInvoker;
#else
#include "Linux/RetSpoofInvoker.h"
using RetSpoofInvoker = linux_platform::RetSpoofInvoker;
#endif

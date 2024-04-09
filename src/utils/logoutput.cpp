/** Copyright (C) 2013 Ultimaker - Released under terms of the AGPLv3 License */
#include <stdio.h>
#include <stdarg.h>

#include <engine/console.h>
#include <engine/logger.h>

#ifdef _OPENMP
    #include <omp.h>
#endif // _OPENMP
#include "logoutput.h"

namespace cura {

static int verbose_level;
static bool progressLogging;

void increaseVerboseLevel()
{
    verbose_level++;
}

void enableProgressLogging()
{
    progressLogging = true;
}

void logError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    #pragma omp critical
    {
		Console::Print(Console::StyleError, "[ERROR] ");
		Console::Print(Console::StyleError, fmt, args);
    }

    va_end(args);
}

void logWarning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    #pragma omp critical
    {
		Console::Print(Console::StyleError, "[WARNING] ");
		Console::Print(Console::StyleError, fmt, args);
    }

    va_end(args);
}

void logAlways(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
	#pragma omp critical
    {
		Console::Print(Console::StyleCura, fmt, args);
    }
    
	va_end(args);
}

void log(const char* fmt, ...)
{
    if (verbose_level < 1)
        return;

	va_list args;
    va_start(args, fmt);

	#pragma omp critical
    {
		Console::Print(Console::StyleCura, fmt, args);
    }
    
	va_end(args);
}

void logDebug(const char* fmt, ...)
{
    if (verbose_level < 2)
        return;

	va_list args;
	va_start(args, fmt);

	#pragma omp critical
    {
		Console::Print(Console::StyleCura, "[DEBUG] ");
		Console::Print(Console::StyleCura, fmt, args);
	}

	va_end(args);
}

void logProgress(const char* type, int value, int maxValue, float percent)
{
    if (!progressLogging)
        return;

    #pragma omp critical
    {
		Console::Print(Console::StyleCura, fmt::sprintf("Progress:%s:%i:%i \t%f%%\n", type, value, maxValue, percent).c_str());
    }
}

}//namespace cura

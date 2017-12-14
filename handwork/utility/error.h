// Provide common error logging support.

#pragma once

#include "utility.h"

namespace handwork
{
	// Error Reporting Declarations
	void Warning(const char *, ...);
	void Error(const char *, ...);

}	// namespace handwork
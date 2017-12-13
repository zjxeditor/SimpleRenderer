// Read pure float value from file.

#include "floatfile.h"
#include <ctype.h>
#include <stdlib.h>

namespace handwork
{
	bool ReadFloatFile(const char *filename, std::vector<float> *values)
	{
		FILE *f = fopen(filename, "r");
		if (!f) 
		{
			std::cout << "Error: unable to open file " << filename << std::endl;
			return false;
		}

		int c;
		bool inNumber = false;
		char curNumber[32];
		int curNumberPos = 0;
		int lineNumber = 1;
		while ((c = getc(f)) != EOF) 
		{
			if (c == '\n') ++lineNumber;
			if (inNumber) 
			{
				if(curNumberPos >= (int)sizeof(curNumber))
				{
					std::cout << "Overflowed buffer for parsing number in file: " << filename << ", at line " << lineNumber << std::endl;
				}
				if (isdigit(c) || c == '.' || c == 'e' || c == '-' || c == '+')
					curNumber[curNumberPos++] = c;
				else 
				{
					curNumber[curNumberPos++] = '\0';
					values->push_back(atof(curNumber));
					inNumber = false;
					curNumberPos = 0;
				}
			}
			else 
			{
				if (isdigit(c) || c == '.' || c == '-' || c == '+') 
				{
					inNumber = true;
					curNumber[curNumberPos++] = c;
				}
				else if (c == '#') 
				{
					while ((c = getc(f)) != '\n' && c != EOF) { }
					++lineNumber;
				}
				else if (!isspace(c)) 
				{
					std::cout << "Warning: unexpected text found at line " << lineNumber << " of float file " << filename << std::endl;
				}
			}
		}
		fclose(f);
		return true;
	}

}	// namespace handwork

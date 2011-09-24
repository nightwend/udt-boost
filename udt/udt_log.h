#pragma once

#ifdef RECORD_LOG
#include <iostream>
#include <string>
#include <boost/current_function.hpp>
inline std::string getShortFuncName(const char *longFuncName)
{
	std::string funcName(longFuncName);
	std::string::size_type q = funcName.find('(');
	if (q!=std::string::npos)
	{
		funcName.erase(q);
		q = funcName.rfind(' ');
		if (q!=std::string::npos)
		{
			std::string::size_type p = funcName.rfind('>');
			if (p!=std::string::npos && p>q)
			{
				// we got 
				int quotCount = 1;
				for (q = p-1;q>0;--q)
				{
					switch(funcName[q])
					{
					case '>':
						++quotCount;
						break;
					case '<':
						--quotCount;
						break;
					case ' ':
						if (quotCount==0)
						{
							funcName.erase(0, q+1);
							return funcName;
						}
					default:
						break;
					}
				}
			}
			else
			{
				funcName.erase(0, q+1);
			}
		}
	}
	return funcName;
}
#define LOG_FUNC_SCOPE_C()
#define LOG_VAR(s) LOG(""#s"" <<" = "<< s
#else
#define LOG_FUNC_SCOPE_C()
#define LOG_VAR(s)
#endif

#define LOG_VAR_C LOG_VAR
#define LOG_WARN_C LOG_VAR
#define LOG_INFO_C LOG_VAR
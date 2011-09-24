#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	20116-17
*/
#include <Windows.h>

struct micro_second
{
	enum {scale = 1000000};
};

struct milli_second
{
	enum {scale = 1000};
};

struct second
{
	enum{scale = 1};
};

template<class Unit>
class ticker
{
public:

	enum
	{
		scale = Unit::scale,
	};
	
	ticker()
	{
		if (frequency_.QuadPart == 0)
		{
			/*
			[out] Pointer to a variable that receives the current performance-counter frequency,
			in counts per second. If the installed hardware does not support a high-resolution 
			performance counter, this parameter can be zero. 
			*/
			QueryPerformanceFrequency(&frequency_);
		}
	}

	unsigned long long tic()
	{
		if (frequency_.QuadPart != 0 &&
			QueryPerformanceCounter(&start_counter_))
		{
			return start_counter_.QuadPart * scale / frequency_.QuadPart;
		}
		else
		{
			return 0;
		}
	}

	unsigned long long toc()
	{
		if (frequency_.QuadPart != 0 && 
			QueryPerformanceCounter(&stop_counter_))
		{
			return stop_counter_.QuadPart * scale / frequency_.QuadPart;
		}
		else
		{
			return 0;
		}
	}

	unsigned long long duration()
	{
		if (frequency_.QuadPart != 0)
		{
			return (stop_counter_.QuadPart - start_counter_.QuadPart) * scale / frequency_.QuadPart;
		}
		else
		{
			return 0;
		}
	}

private:

	static LARGE_INTEGER frequency_;

	LARGE_INTEGER start_counter_;
	
	LARGE_INTEGER stop_counter_;
};

template<class Unit>
LARGE_INTEGER ticker<Unit>::frequency_ = { 0 };

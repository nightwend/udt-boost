#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-6-29
*/

#include <boost/detail/interlocked.hpp>

template <class T>
class singleton
{
	static T* s_instance;	//for different type, the address of s_instance will be different.

public:

	static T* get_instance()
	{	
		T* instance = s_instance;	//capture-try-discard model.
		if (instance == NULL)
		{
			instance = new T();
			if(BOOST_INTERLOCKED_COMPARE_EXCHANGE_POINTER((volatile PVOID *)&s_instance, instance, NULL) != NULL)
			{
				delete instance;	//someone has initiated s_instance, discard ours.
			}
		}
		return s_instance;
	}

	static void release_instance()
	{
		T* instance = s_instance;	//capture-try-discard model.
		if (instance != NULL)
		{
			if(BOOST_INTERLOCKED_COMPARE_EXCHANGE_POINTER((volatile PVOID *)&s_instance, NULL, instance) == instance)
			{
				delete instance;	//s_instance still s_instance, delete it.
			}
		}
	}
};

template <class T>
T* singleton<T>::s_instance = NULL;	//for different type, the address of s_instance will be different.

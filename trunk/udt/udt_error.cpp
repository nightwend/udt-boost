#include "udt_error.h"

const boost::system::error_category& get_udt_category()
{
	const static udt_error_category error_category;
	return error_category;
}

const char* udt_error_category::name() const
{
	return "udt"; 
}

std::string udt_error_category::message(int ev) const
{
	return "";
}

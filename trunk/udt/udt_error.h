#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-07-04
*/

#include <boost/system/error_code.hpp>
#include <boost/asio/error.hpp>
#include <string>

namespace udt_error
{
	enum errors
	{
		succeed = 0,
		eof = boost::asio::error::eof,
		invalid_argument = boost::asio::error::invalid_argument,
		connection_reset = boost::asio::error::connection_reset,
		timed_out = boost::asio::error::timed_out,
		operation_aborted = boost::asio::error::operation_aborted,
		closed,
		disconnected,
		alread_created,
		malformed_packet,
		not_bind,
	};
};

namespace boost { namespace system {

	template<> struct is_error_code_enum</*libim::*/udt_error::errors>
	{ static const bool value = true; };
} }

struct udt_error_category : boost::system::error_category
{
	virtual const char* name() const;
	virtual std::string message(int ev) const;
	virtual boost::system::error_condition default_error_condition(int ev) const
	{ return boost::system::error_condition(ev, *this); }
};

const boost::system::error_category& get_udt_category();

namespace udt_error
{
	inline boost::system::error_code make_error_code(errors e)
	{
		return boost::system::error_code(e, get_udt_category());
	}
}

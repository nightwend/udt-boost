#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-7-14
*/
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/ip/basic_resolver.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/weak_ptr.hpp>
#include "udt_log.h"

template <class InternetProtocol, class Endpoints>
size_t get_local_endpoints_v4(boost::asio::io_service& ios, const char* service, Endpoints& eps)
{
	eps.clear();
	boost::system::error_code ignored;
	boost::asio::ip::basic_resolver_query<InternetProtocol> query(boost::asio::ip::host_name(ignored), service);
	boost::asio::ip::basic_resolver<InternetProtocol> resolver(ios);
	boost::asio::ip::basic_resolver_iterator<InternetProtocol> i = resolver.resolve(query, ignored), end;
	while (i != end)
	{
		boost::asio::ip::basic_endpoint<InternetProtocol> ep = i->endpoint();
		if (ep.protocol() == InternetProtocol::v4() && 
			std::find(eps.begin(), eps.end(), ep) == eps.end())
		{
			LOG_VAR(ep);
			eps.push_back(ep);
		}
		++i;
	}
	return eps.size();
}

long long htonll(long long v);	//assume the arch is little endian.

long long ntohll(long long v);

class win_event
{
public:
	// Constructor.
	win_event(bool initialState=false);
	~win_event();

	operator bool() const;
	bool operator !() const;

	// Signal the event.
	void signal();

	// Reset the event.
	void clear();

	// Wait for the event to become signalled.
	void wait();
private:
	HANDLE event_;
};

template <typename Handler, typename ResultType>
struct invoker_wrapper
{
	invoker_wrapper(const Handler* h, ResultType* r, win_event* e):handler(h), result(r), event(e){}
	void operator()()
	{
		*result = (*handler)();
		event->signal();
	}
private:
	ResultType* result;
	const Handler* handler;
	win_event* event;
};

template<typename Handler, typename ResultType>
void invoke_io_service(boost::asio::io_service& ios, const Handler* handler, ResultType* result)
{
	win_event event;
	ios.dispatch(invoker_wrapper<Handler, ResultType>(handler, result, &event));
	event.wait();
}

template <typename Handler>
struct post_wrapper
{
	post_wrapper(const Handler& h, const boost::weak_ptr<void>& w):handler(h), wptr(w){}
	void operator()()
	{
		if (!wptr.expired())
		{
			handler();
		}
	}
private:
	Handler handler;
	boost::weak_ptr<void> wptr;
};

template<typename Handler>
void post_io_service(boost::asio::io_service& ios, const Handler& handler, const boost::weak_ptr<void>& wptr)
{
	ios.post(post_wrapper<Handler>(handler, wptr));
};

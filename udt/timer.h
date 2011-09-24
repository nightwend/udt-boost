#pragma once

/************************************************************************/
/*
Author:	mailto:gongyiling@myhada.com
Date:	2011/5/4
timer
*/
/************************************************************************/

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include "tracker.h"

class timer
{
public:

	typedef boost::function<void (void)> timer_proc_t;

	timer(boost::asio::io_service& ios);

	virtual ~timer();

	void start(int milli_sec, timer_proc_t timer_proc = timer_proc_t());

	void stop();

	int duration() const{return m_milli_sec;}

protected:

	virtual void timer_proc();

private:

	void async_wait();

	void handle_timeout(const boost::system::error_code& ec, const tracker::weak_refer& wref);

	boost::asio::deadline_timer m_timer;

	timer_proc_t m_timer_proc;

	int m_milli_sec;

	bool m_stopping;

	tracker m_tracker;
};


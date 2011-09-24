#include "timer.h"
#include <boost/bind.hpp>

timer::timer(boost::asio::io_service& ios):
m_timer(ios),
m_milli_sec(0)
{
}

timer::~timer()
{
	stop();
}

void timer::start(int milli_sec, timer_proc_t timer_proc)
{
	m_stopping = false;
	m_milli_sec = milli_sec;
	m_timer_proc = timer_proc;
	async_wait();
}

void timer::async_wait()
{
	m_timer.expires_from_now(boost::posix_time::millisec(m_milli_sec));
	m_timer.async_wait(boost::bind(&timer::handle_timeout, this, _1, m_tracker.get()));
}

void timer::stop()
{
	if (m_stopping)
	{
		return;
	}
	m_stopping = true;
	boost::system::error_code ignored;
	m_timer.cancel(ignored);
}

void timer::handle_timeout(const boost::system::error_code& ec, const tracker::weak_refer& wref)
{
	if (wref.expired())
	{
		return;
	}
	if (!ec && !m_stopping)	//condition order is important!.
	{
		tracker::weak_refer wref = m_tracker.get();
		if (m_timer_proc)
		{
			m_timer_proc();
		}
		else
		{
			timer_proc();
		}
		if (!wref.expired())
		{
			async_wait();
		}
	}
}

void timer::timer_proc()
{
	;//default do nothing
}

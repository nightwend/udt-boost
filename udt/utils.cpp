#include "utils.h"

long long htonll(long long v)	//assume the arch is little endian.
{
	long long ret;
	*(long*)&ret = htonl( *((long*)&v + 1) );
	*((long*)&ret + 1) = htonl( *((long*)&v) );
	return ret;
}

long long ntohll(long long v)
{
	return htonll(v);
}

// Constructor.
win_event::win_event(bool initialState)
: event_(::CreateEvent(0, true, initialState, 0))
{
	if (!event_)
	{
		DWORD last_error = ::GetLastError();
		boost::system::system_error e(
			boost::system::error_code(last_error,
			boost::asio::error::get_system_category()),
			"event");
		boost::throw_exception(e);
	}
}

// Destructor.
win_event::~win_event()
{
	::CloseHandle(event_);
}

win_event::operator bool() const
{
	return WAIT_OBJECT_0 == ::WaitForSingleObject(event_, 0);
}

bool win_event::operator !() const
{
	return WAIT_OBJECT_0 != ::WaitForSingleObject(event_, 0);
}

// Signal the event.
void win_event::signal()
{
	::SetEvent(event_);
}

// Reset the event.
void win_event::clear()
{
	::ResetEvent(event_);
}

// Wait for the event to become signalled.
void win_event::wait()
{
	::WaitForSingleObject(event_, INFINITE);
}
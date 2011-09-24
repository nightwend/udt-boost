#include "udt_cb.h"
#include <boost/assert.hpp>
#include "udt_log.h"

int udt_cb::connect()
{
	/*
	 * Initiate connection to peer.
	 * Create a template for use in transmissions on this connection.
	 * Enter SYN_SENT state, and mark socket as connecting.
	 * Start keep-alive timer, and seed output sequence space.
	 * Send initial segment on connection.
	 */
	LOG_VAR_C(m_state);
	if (m_state != TCPS_CLOSED)
	{
		BOOST_ASSERT(false);
		return -1;
	}
	m_state = TCPS_SYN_SENT;
	m_timers[TCPT_KEEP] = TCPTV_KEEP_INIT;
	m_iss = s_iss;
	tcp_sendseqinit();
	output();
	return 0;
}

int udt_cb::accept()
{
	LOG_VAR_C(m_state);
	if (m_state != TCPS_CLOSED)
	{
		BOOST_ASSERT(false);
		return -1;
	}
	m_state = TCPS_LISTEN;
	return 0;
}

int udt_cb::close()
{
	LOG_VAR_C(m_state);
	m_state = TCPS_CLOSED;
	canceltimers();
	m_fast_timer.stop();
	m_slow_timer.stop();
	return 0;
}

int udt_cb::shutdown()
{
	LOG_VAR_C(m_state);
	switch (m_state)
	{
	case TCPS_ESTABLISHED:	//active close
		{
			m_state = TCPS_FIN_WAIT_1;
			break;
		}
	case TCPS_CLOSE_WAIT:	//passive close
		{
			m_state = TCPS_LAST_ACK;
			break;
		}
	default:
		{
			return -1;
		}
	}
	LOG_VAR_C(m_state);
	output();
	return 0;
}

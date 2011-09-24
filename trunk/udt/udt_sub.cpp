#include "udt_cb.h"
#include <boost/assert.hpp>
#include "udt_socket.h"
#include "udt_log.h"
/*
 * The initial retransmission should happen at rtt + 4 * rttvar.
 * Because of the way we do the smoothing, srtt and rttvar
 * will each average +1/2 tick of bias.  When we compute
 * the retransmit timer, we want 1/2 tick of rounding and
 * 1 extra tick because of +-1/2 tick uncertainty in the
 * firing of the timer.  The bias will give us exactly the
 * 1.5 tick we need.  But, because the bias is
 * statistical, we have to test that we don't drop below
 * the minimum feasible timer (which is 2 ticks).
 * This macro assumes that the value of TCP_RTTVAR_SCALE
 * is the same as the multiplier for rttvar.
 */

void udt_cb::xmit_timer(short rtt)
{
	LOG_VAR_C(rtt);
	if (m_srtt != 0) 
	{
		/*
		* srtt is stored as fixed point with 3 bits after the
		* binary point (i.e., scaled by 8).  The following magic
		* is equivalent to the smoothing algorithm in rfc793 with
		* an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
		* point).  Adjust rtt to origin 0.
		*/
		short delta  = rtt - 1 - (m_srtt >> TCP_RTT_SHIFT);
		if ((m_srtt += delta) <= 0)
			m_srtt = 1;
		/*
		* We accumulate a smoothed rtt variance (actually, a
		* smoothed mean difference), then set the retransmit
		* timer to smoothed rtt + 4 times the smoothed variance.
		* rttvar is stored as fixed point with 2 bits after the
		* binary point (scaled by 4).  The following is
		* equivalent to rfc793 smoothing with an alpha of .75
		* (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
		* rfc793's wired-in beta.
		*/
		if (delta < 0)
			delta = -delta;
		delta -= (m_rttvar >> TCP_RTTVAR_SHIFT);
		if ((m_rttvar += delta) <= 0)
			m_rttvar = 1;
	}
	else 
	{
		/* 
		* No rtt measurement yet - use the unsmoothed rtt.
		* Set the variance to half the rtt (so our first
		* retransmit happens at 3*rtt).
		*/
		m_srtt = rtt << TCP_RTT_SHIFT;
		m_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
	}
	m_rtt = 0;
	m_rxtshift = 0;
	LOG_VAR_C(m_srtt);
	LOG_VAR_C(m_rttvar);
	LOG_VAR_C(TCP_REXMTVAL());
	/*
	* the retransmit should happen at rtt + 4 * rttvar.
	* Because of the way we do the smoothing, srtt and rttvar
	* will each average +1/2 tick of bias.  When we compute
	* the retransmit timer, we want 1/2 tick of rounding and
	* 1 extra tick because of +-1/2 tick uncertainty in the
	* firing of the timer.  The bias will give us exactly the
	* 1.5 tick we need.  But, because the bias is
	* statistical, we have to test that we don't drop below
	* the minimum feasible timer (which is 2 ticks).
	*/
	m_rxtcur = rexmt_timer_value();
	LOG_VAR_C(m_rxtcur);
	/*
	* We received an ack for a packet that wasn't retransmitted;
	* it is probably safe to discard any error indications we've
	* received recently.  This isn't quite right, but close enough
	* for now (a route might have failed after we sent a segment,
	* and the return path might not be symmetrical).
	*/
}

bool udt_cb::can_persist()
{
	return m_sk->total_buffer_size() &&
		m_timers[TCPT_PERSIST] == 0 && 
		m_timers[TCPT_REXMT] == 0;
}

void udt_cb::enter_persist()
{
	int t = ((m_srtt >> 2) + m_rttvar) >> 1;
	LOG_VAR_C(t);
	BOOST_ASSERT(m_timers[TCPT_REXMT] == 0);
	/*
	 * Start/restart persistance timer.
	 */
	m_timers[TCPT_PERSIST] = TCPT_RANGESET(t * s_tcp_backoff[m_rxtshift],
	    TCPTV_PERSMIN, TCPTV_PERSMAX);
	if (m_rxtshift < TCP_MAXRXTSHIFT)
		m_rxtshift++;
}

void udt_cb::respond(udt_seq ack, udt_seq seq, int flags)
{
	udt_hdr ti;
	ti.uh_ver = 0;
	ti.uh_flags = flags;
	ti.uh_len = 0;
	ti.uh_off = sizeof(udt_hdr);
	ti.uh_win = m_sk->rcv_buf().acc();
	ti.uh_ack = ack;
	ti.uh_seq = seq;
	ti.uh_pseq = m_pseq;
	ti.hton();
	udp_output(&ti, sizeof(ti), NULL, 0);
}

void udt_cb::drop(const boost::system::error_code& e)
{
	LOG_VAR_C(e);
	LOG_VAR_C(m_state);
	if (e)
	{
		m_last_error = e;
	}
	m_sk->on_disconnected();
	m_state = TCPS_CLOSED;
}

void udt_cb::canceltimers()
{
	m_timers[TCPT_REXMT] = 0;
	m_timers[TCPT_PERSIST] = 0;
	m_timers[TCPT_KEEP] = 0;
	m_timers[TCPT_2MSL] = 0;
}

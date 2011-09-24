#include <boost/bind.hpp>
#include <boost/asio/error.hpp>
#include <boost/assert.hpp>
#include "udt_cb.h"
#include "udt_buf.h"
#include "udt_socket.h"
#include "udt_log.h"
#include "udt_error.h"

void udt_cb::handle_fast_timo()
{
	if (m_flags & TF_DELACK)
	{
		LOG_VAR_C(m_flags);
		m_flags &= ~TF_DELACK;
		m_flags |= TF_ACKNOW;
		output();
	}
}

void udt_cb::clear_snd_buf_flags()
{
	LOG_VAR_C(m_retrans_data);
	udt_snd_buf* sb = &(m_sk->snd_buf());
	for (udt_buf::list_packets_iter_t i = sb->begin(); i != sb->end(); ++i)
	{
		udt_hdr* ti = get_hdr(i);
		if (seq_ge(ti->uh_seq, m_snd_max))
		{
			break;
		}
		ti->uh_flags = 0;
	}
	m_retrans_data = 0;
}

void udt_cb::handle_slow_timo()
{
	for (int i = 0; i < TCPT_NTIMERS; i++)
	{
		if (m_timers[i] && --m_timers[i] == 0)
		{
			switch (i)
			{
			case TCPT_2MSL:
				{
					LOG_INFO_C("TCPT_2MSL expired");
					drop(udt_error::timed_out);
					break;
				}
					/*
					 * Retransmission timer went off.  Message has not
					 * been acked within retransmit interval.  Back off
					 * to a longer retransmit interval and retransmit one segment.
					 */
			case TCPT_REXMT:
				{
					if (++m_rxtshift > TCP_MAXRXTSHIFT)
					{
						m_rxtshift = TCP_MAXRXTSHIFT;
						drop(udt_error::timed_out);
						break;
					}
					LOG_VAR_C(m_rxtshift);
					m_rxtcur = rexmt_timer_value();
					m_timers[TCPT_REXMT] = m_rxtcur;
					LOG_VAR_C(m_rxtcur);
					/*
					 * If losing, let the lower level know and try for
					 * a better route.  Also, if we backed off this far,
					 * our srtt estimate is probably bogus.  Clobber it
					 * so we'll take the next rtt measurement as our srtt;
					 * move the current srtt into rttvar to keep the current
					 * retransmit times until then.
					 */
					if (m_rxtshift > TCP_MAXRXTSHIFT / 4)
					{
						m_rttvar += (m_srtt >> TCP_RTT_SHIFT);
						m_srtt = 0;
					}
					m_snd_nxt = m_snd_fack = m_snd_una;
					m_rtt = 0;
					handle_packet_lost();
					//slow_start();
					clear_snd_buf_flags();
					output();
					break;
				}
			case TCPT_PERSIST:
				{
					LOG_INFO_C("TCPT_PERSIST expired");
					enter_persist();
					validate_first_pkt();
					m_force = true;
					output();	//force output a window probe segment.
					m_force = false;
					break;
				}
			case TCPT_KEEP:
				{
					LOG_INFO_C("TCPT_KEEP expired");
					if (m_state < TCPS_ESTABLISHED)	//during connection state, drop the connection.
					{
						drop(udt_error::timed_out);
					}
					else if (m_state <= TCPS_CLOSE_WAIT && m_sk->so_options_ & udt_socket::UDT_KEEPALIVE)
					{
						if (m_idle >= s_keepidle + s_maxidle)
						{
							drop(udt_error::timed_out);
							break;
						}
						respond(m_rcv_nxt, m_snd_una - 1, udt_hdr::TH_ACK);	//send a keepalive to peer.
						m_timers[TCPT_KEEP] = s_keepintvl;
					}
					else
					{
						m_timers[TCPT_KEEP] = s_keepidle;
					}
					break;
				}
				break;
			default:
				BOOST_ASSERT(false);
				return;
			}
		}
	}
	if (m_rtt)
	{
		m_rtt++;
	}
	m_idle++;
	m_tick++;
}

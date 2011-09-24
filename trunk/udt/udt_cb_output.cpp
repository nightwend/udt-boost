#include "udt_cb.h"
#include <boost/assert.hpp>
#include "udt_socket.h"
#include "udt_buf.h"
#include <algorithm>
#include "udt_option.h"
#include "udt_log.h"

size_t udt_cb::output()
{
	LOG_VAR_C(m_state);
	if (m_state == TCPS_CLOSED)
	{
		return 0;
	}
	udt_snd_buf* sb = &(m_sk->snd_buf());
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	/*
	 * Determine length of data that should be transmitted,
	 * and flags that will be used.
	 * If there is some data or critical controls (SYN, RST)
	 * to send, then transmit; otherwise, investigate further.
	 */
	bool idle = m_snd_max == m_snd_una;
	if (idle && m_idle >= m_rxtcur)
	{
		/*
		 * We have been idle for "a while" and no acks are
		 * expected to clock out any data we send --
		 * slow start to get ack "clock" running again.
		 */
		LOG_INFO_C("idle for a while, slow start");
		slow_start();
	}

	size_t sent_bytes = 0;

	int send_packets_clamp = 10;

again:

	bool sendalot = false;
	
	long can_send = can_send_bytes();

	if (m_sk->pending_data() && (sb->seq() < can_send + m_snd_nxt || !sb->above_lowat()))
	{
		m_sk->consume_pending_data();
	}
	LOG_VAR_C(can_send);

	if (m_force)	//in persist state
	{
		LOG_INFO_C("force output");
		BOOST_ASSERT(m_snd_una == m_snd_nxt);
		if (can_send < (long)m_maxseg)
		{
			can_send = m_maxseg;	//force output 1 packet.
		}
	}

	udt_packet* pkt = NULL;

	if (can_send >= (long)m_maxseg)
	{
		pkt = get_snd_pkt(can_send);
	}
	else if ((long)m_snd_wnd < (long)m_maxseg)
	{
		m_snd_nxt = m_snd_una;
		m_timers[TCPT_REXMT] = 0;
	}

	int len = 0;
	if (pkt != NULL)
	{
		len = pkt->hdr.uh_len;
		if (!m_force)
		{
			leave_persist();
			if (can_send > len)
			{
				sendalot = true;
			}
		}
	}

	int flags = s_tcp_outflags[m_state];
	
	if (seq_lt(m_snd_nxt + len, sb->seq() + m_sk->pending_data()))
	{
		LOG_VAR_C(m_snd_una);
		flags &= ~udt_hdr::TH_FIN;
	}

	if (len) 
	{
		goto send;
	}
	/*
	 * Compare available window to amount of window
	 * known to peer (as advertised window less
	 * next expected input).  If the difference is at least two
	 * max size segments, or at least 50% of the maximum possible
	 * window, then want to send a window update to peer.
	 */
	long rcv_win = (long)rb->acc();
	if (rcv_win > 0) 
	{
		/* 
		 * "adv" is the amount we can increase the window,
		 * taking into account that we are limited by
		 * TCP_MAXWIN << tp->rcv_scale.
		 */
		long adv = std::min<long>(rcv_win, TCP_MAXWIN) - (m_rcv_adv - m_rcv_nxt);

		if (adv >= (long) (2 * m_maxseg))
			goto send;
		if (2 * adv >= (long) rb->hiwat())
			goto send;
	}

	/*
	 * Send if we owe peer an ACK.
	 */
	if (m_flags & TF_ACKNOW)
		goto send;
	if (flags & (udt_hdr::TH_SYN | udt_hdr::TH_RST))
		goto send;

	/*
	 * If our state indicates that FIN should be sent
	 * and we have not yet done so, or we're retransmitting the FIN,
	 * then we need to send.
	 */
	if (flags & udt_hdr::TH_FIN &&
		((m_flags & TF_SENTFIN) == 0 || m_snd_nxt == m_snd_una))
		goto send;

	if (can_persist()) 
	{
		m_rxtshift = 0;
		enter_persist();
	}

	/*
	 * No reason to send a segment, just return.
	 */
	return sent_bytes;

send:

	size_t hdrlen = sizeof(udt_hdr);
	if (flags & udt_hdr::TH_SYN)
	{
		m_snd_nxt = m_iss;
	}

	/*
	 * Fill in fields, remembering maximum advertised
	 * window for use in delaying messages about window sizes.
	 * If resending a FIN, be sure not to use a new sequence number.
	 */
	if (flags & udt_hdr::TH_FIN && TCPS_HAVESNDFIN() && 
		m_snd_nxt == m_snd_max)
	{
		m_snd_nxt--;
	}
	/*
	 * If we are doing retransmissions, then snd_nxt will
	 * not reflect the first unsent octet.  For ACK only
	 * packets, we do not want the sequence number of the
	 * retransmitted packet, we want the sequence number
	 * of the next unsent octet.  So, if there is no data
	 * (and no SYN or FIN), use snd_max instead of snd_nxt
	 * when filling in ti_seq.  But if we are in persist
	 * state, snd_max might reflect one byte beyond the
	 * right edge of the window, so use snd_nxt in that
	 * case, since we know we aren't doing a retransmission.
	 * (retransmit and persist are mutually exclusive...)
	 */
	char buffer_[udt_hdr::MAX_HEADER_SIZE];
	udt_hdr* ti = (udt_hdr*)buffer_;
	size_t optlen = 0;
	if (len || (flags & (udt_hdr::TH_SYN|udt_hdr::TH_FIN)) || m_timers[TCPT_PERSIST])
	{
		ti->uh_seq = m_snd_nxt;
		LOG_VAR_C(m_snd_nxt);
	}
	else
	{
		ti->uh_seq = m_snd_max;
		LOG_VAR_C(m_snd_max);
	}
	ti->uh_ack = m_rcv_nxt;
	ti->uh_flags = flags;
	LOG_VAR_C(flags);
	ti->uh_len = (u_short)len;
	LOG_VAR_C(len);
	/*
	 * Calculate receive window.  Don't shrink window,
	 * but avoid silly window syndrome.
	 */
	if (rcv_win < (long)(rb->hiwat() / 4) && rcv_win < (long)m_maxseg)
		rcv_win = 0;
	if (rcv_win > (long)TCP_MAXWIN)
		rcv_win = (long)TCP_MAXWIN;
	if (rcv_win < (long)(m_rcv_adv - m_rcv_nxt))
		rcv_win = (long)(m_rcv_adv - m_rcv_nxt);
	ti->uh_win = (u_short) rcv_win;
	ti->uh_ver = 0x00;
	ti->uh_pseq = m_pseq;
	if (flags & udt_hdr::TH_ACK)
	{
		size_t max_opt_len = udt_hdr::MAX_HEADER_SIZE - sizeof(udt_hdr);
		optlen = max_opt_len - udt_cb::compose_options((char*)ti + sizeof(udt_hdr), max_opt_len);
	}

	ti->uh_off = sizeof(udt_hdr) + optlen;
	ti->hton();
	/*
	 * In transmit state, time the transmission and arrange for
	 * the retransmit.  In persist state, just set snd_max.
	 */
	if (m_force == false || m_timers[TCPT_PERSIST] == 0) 
	{
		udt_seq startseq = m_snd_nxt;

		/*
		 * Advance snd_nxt over sequence space of this segment.
		 */
		if (flags & (udt_hdr::TH_SYN | udt_hdr::TH_FIN)) 
		{
			if (flags & udt_hdr::TH_SYN)
				m_snd_nxt++;
			if (flags & udt_hdr::TH_FIN) 
			{
				m_snd_nxt++;
				m_flags |= TF_SENTFIN;
			}
		}
		m_snd_nxt += len;
		if (seq_gt(m_snd_nxt, m_snd_max))
		{
			m_snd_max = m_snd_nxt;
			/*
			 * Time this transmission if not a retransmission and
			 * not currently timing anything.
			 */
			if (m_rtt == 0)
			{
				m_rtt = 1;
				m_rtseq = startseq;
			}
		}

		/*
		 * Set retransmit timer if not currently set,
		 * and not doing an ack or a keep-alive probe.
		 * Initial value for retransmit timer is smoothed
		 * round-trip time + 2 * round-trip time variance.
		 * Initialize shift counter which is used for backoff
		 * of retransmit time.
		 */
		if (m_timers[TCPT_REXMT] == 0 &&
		    (m_snd_nxt != m_snd_una))
		{
			m_timers[TCPT_REXMT] = m_rxtcur;
			leave_persist();
		}
		else
		{
			LOG_INFO_C("rexmt not set");
			LOG_VAR_C(m_snd_nxt);
			LOG_VAR_C(m_timers[TCPT_REXMT]);
			LOG_VAR_C(m_snd_una);
		}
	} 
	else if (seq_gt(m_snd_nxt + len, m_snd_max))
		m_snd_max = m_snd_nxt + len;

	char* data = NULL;
	if (pkt)
	{
		BOOST_ASSERT(ti->uh_seq == htonl(pkt->hdr.uh_seq));
		data = pkt->data_ptr();
	}
	udp_output(ti, sizeof(udt_hdr) + optlen, data, len);
	sent_bytes += len;
	/*
	 * Data sent (as far as we can tell).
	 * If this advertises a larger window than any other segment,
	 * then remember the size of the advertised window.
	 * Any pending ACK has now been sent.
	 */
	if (rcv_win > 0 && seq_gt(m_rcv_nxt + rcv_win, m_rcv_adv))
		m_rcv_adv = m_rcv_nxt + rcv_win;
	m_flags &= ~(TF_ACKNOW | TF_DELACK | TF_NEED_OUTPUT);
	if (sendalot && --send_packets_clamp > 0)
		goto again;
	return sent_bytes;
}

u_long udt_cb::can_send_bytes()	//how many can send.
{
	long ret = std::min((long)(m_snd_cwnd - bytes_inflight()),	//limited by network capacity.
		remain_snd_wnd());		//limited by peer's buffer.
	if (ret < 0)
	{
		ret = 0;
	}
	return ret;
}

udt_packet* udt_cb::get_snd_pkt(size_t len)	//what to send.
{
	udt_snd_buf* sb = &(m_sk->snd_buf());
	for(udt_buf::list_packets_iter_t i = sb->begin(); i != sb->end(); ++i)
	{
		udt_hdr* ti = get_hdr(i);
		size_t tilen = ti->uh_len;
		if (seq_ge(ti->uh_seq, m_snd_nxt) && !has_rexmt(ti->uh_flags))	//off belongs to [start, end) => off - end < 0
		{				//
			if (tilen <= len)
			{
				if (seq_lt(ti->uh_seq, m_snd_max))	//do retransmit.
				{
					BOOST_ASSERT(!has_rexmt(ti->uh_flags));
					BOOST_ASSERT(seq_le(ti->uh_seq + ti->uh_len, m_snd_max));
					ti->uh_flags |= udt_hdr::TH_RETRANS;
					m_retrans_data += tilen;
					BOOST_ASSERT(ti->uh_seq != 0);
				}
				m_snd_nxt = ti->uh_seq;
				return get_pkt(i);
			}
			else
			{
				return NULL;
			}
		}
	}
	return NULL;
}

void udt_cb::validate_first_pkt()
{
	udt_snd_buf* sb = &(m_sk->snd_buf());
	if (sb->begin() != sb->end())
	{
		udt_hdr* ti = get_hdr(sb->begin());
		if (has_rexmt(ti->uh_flags))
		{
			m_retrans_data -= ti->uh_len;
		}
		ti->uh_flags = 0;
	}
}

size_t udt_cb::compose_options(char* ptr, size_t remain_len)
{
	size_t new_remain_len = compose_cwnd_option(ptr, remain_len);
	size_t consumed = remain_len - new_remain_len;
	return compose_sack_option(ptr + consumed, new_remain_len);
}

size_t udt_cb::compose_cwnd_option(char* ptr, size_t remain_len)
{
	LOG_VAR_C(remain_len);
	if (remain_len >= sizeof(udt_option_cwnd))
	{
		udt_option_cwnd* opt_cwnd = (udt_option_cwnd*)ptr;
		opt_cwnd->kind = udt_option::TCPOPT_CWND;
		opt_cwnd->len = sizeof(udt_option_cwnd);
		opt_cwnd->cwnd = htonl(m_snd_cwnd);
		remain_len -= sizeof(udt_option_cwnd);
	}
	return remain_len;
}

size_t udt_cb::compose_sack_option(char* ptr, size_t remain_len)
{
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	size_t new_remain_len = remain_len;
	LOG_VAR_C(rb->reasm_pkts().empty());
	if (!rb->reasm_pkts().empty())
	{
		udt_option_sack* opt_sack = (udt_option_sack*)ptr;
		new_remain_len = opt_sack->compose(rb->reasm_pkts().rbegin(), rb->reasm_pkts().rend(), remain_len);
		LOG_VAR_C(new_remain_len);
		if (new_remain_len < remain_len)
		{
			opt_sack->hton();
		}
	}
	return new_remain_len;
}

void udt_cb::udp_output(const udt_hdr* hdr, size_t hdr_len, const void* data, size_t data_len)
{

#ifdef _DEBUG
	static bool flag = true;
	if (flag && hdr->uh_seq == htonl(1))
	{
		flag = false;
		//return;
	}
#endif

	udt_socket::next_layer_t* udp_sk = m_sk->next_layer();
	std::vector<udt_socket::const_buffer_t> cbfs;
	if (hdr != NULL && hdr_len != 0)
	{
		cbfs.push_back(boost::asio::buffer(hdr, hdr_len));
	}
	if (data != NULL && data_len != 0)
	{
		cbfs.push_back(boost::asio::buffer(data, data_len));
	}
	boost::system::error_code e;
	udp_sk->send_to(cbfs,
		m_sk->peer_endpoint(), 0, e);
}

#include "udt_cb.h"
#include <boost/assert.hpp>
#include "udt_buf.h"
#include "udt_socket.h"
#include "udt_defs.h"
#include "udt_option.h"
#include "udt_log.h"
#include "udt_error.h"
/*
 * Insert segment ti into reassembly queue of tcp with
 * control block tp.  Return TH_FIN if reassembly now includes
 * a segment with FIN.  The macro form does the common case inline
 * (segment is the next to be received on an established connection,
 * and the queue is empty), avoiding linkage into and removal
 * from the queue and repetition of various conversions.
 * Set DELACK for segments received in order, but ack immediately
 * when segments are out of order (so fast retransmit can work).
 */
void udt_cb::TCP_REASS(udt_packet* pkt)
{
	LOG_FUNC_SCOPE_C();
	udt_hdr* ti = &(pkt->hdr);
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	udt_buf::list_packets_t& reasm_pkts = rb->reasm_pkts();
	if (ti->uh_seq == m_rcv_nxt &&
		reasm_pkts.empty()) 
	{
		if(!rb->append(pkt))
		{
			BOOST_ASSERT(false);
			return;
		}
		m_rcv_nxt += ti->uh_len;
		m_flags |= TF_DELACK | TF_PACKET_CONSUMED;
		if (ti->uh_len)
		{
			m_flags |= TF_NOTIFY_READ;
		}
	}
	else 
	{
		tcp_reass(pkt);
		m_flags |= TF_ACKNOW;
	}
}

void check_rcv_pkts(udt_buf::list_packets_t& rcv_pkts)
{
	if (rcv_pkts.empty())
	{
		return;
	}
	udt_buf::list_packets_iter_t i = rcv_pkts.begin();
	udt_seq nxt_seq = (*i)->hdr.uh_seq + (*i)->hdr.uh_len;
	++i;
	for (; i != rcv_pkts.end(); ++i)
	{
		udt_hdr* ti = get_hdr(i);
		if (nxt_seq != ti->uh_seq)
		{
			BOOST_ASSERT(false);
		}
		nxt_seq = ti->uh_seq + ti->uh_len;
	}
}

void check_reasm_pkts(udt_buf::list_packets_t& reasm_pkts)
{
	if (reasm_pkts.empty())
	{
		return;
	}
	udt_buf::list_packets_iter_t i = reasm_pkts.begin();
	udt_seq nxt_seq = (*i)->hdr.uh_seq + (*i)->hdr.uh_len;
	++i;
	for (; i != reasm_pkts.end(); ++i)
	{
		udt_hdr* ti = get_hdr(i);
		if (seq_gt(nxt_seq, ti->uh_seq))
		{
			BOOST_ASSERT(false);
		}
		nxt_seq = ti->uh_seq + ti->uh_len;
	}
}

void udt_cb::tcp_reass(udt_packet* pkt)
{
	LOG_FUNC_SCOPE_C();
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	udt_hdr* hdr = &(pkt->hdr);
	udt_buf::list_packets_t& reasm_pkts = rb->reasm_pkts();
	udt_buf::list_packets_iter_t i = reasm_pkts.begin();
	while (i != reasm_pkts.end() && seq_lt(get_hdr(i)->uh_seq, hdr->uh_seq)) ++i;
	if (i != reasm_pkts.end())	//(*i)->hdr.uh_seq >= hdr->uh_seq.
	{
		if (get_hdr(i)->uh_seq == hdr->uh_seq)
		{
			LOG_INFO_C("discard");
			LOG_VAR_C(hdr->uh_seq);
			return;	//duplicate packet.
		}
		reasm_pkts.insert(i, pkt);	//(*q)->hdr.uh_seq > hdr->uh_seq.
	}
	else	//we are in the end, 
			//condition seq_lt((*i)->hdr.uh_seq, hdr->uh_seq)
			//hold for the last element in reasm_pkts, so just push packet to the end.
	{
		reasm_pkts.push_back(pkt);
	}
#ifdef _DEBUG
	check_reasm_pkts(reasm_pkts);
#endif
	m_flags |= TF_PACKET_CONSUMED;
	return merge_reasm_pkts();
}

void udt_cb::merge_reasm_pkts()
{
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	udt_buf::list_packets_t& reasm_pkts = rb->reasm_pkts();
	udt_buf::list_packets_iter_t i = reasm_pkts.begin();
	udt_seq old_rcv_nxt = m_rcv_nxt;
	LOG_VAR_C(m_rcv_nxt);
	for (; i != reasm_pkts.end() && (*i)->hdr.uh_seq == m_rcv_nxt; ++i)
	{
		udt_hdr* ti = get_hdr(i);;
		if (!rb->append(get_pkt(i)))
		{
			BOOST_ASSERT(false);
			break;
		}
		m_rcv_nxt += ti->uh_len;
	}
	if (old_rcv_nxt != m_rcv_nxt)
	{
		m_flags |= TF_NOTIFY_READ;
	}
	LOG_VAR_C(m_rcv_nxt);
#ifdef _DEBUG
	check_rcv_pkts(rb->m_pkts);
#endif
	reasm_pkts.erase(reasm_pkts.begin(), i);
}

bool udt_cb::do_header_prediction(udt_packet* pkt)
{
	/* 
	 * Header prediction: check for the two common cases
	 * of a uni-directional data xfer.  If the packet has
	 * no control flags, is in-sequence, the window didn't
	 * change and we're not retransmitting, it's a
	 * candidate.  If the length is zero and the ack moved
	 * forward, we're the sender side of the xfer.  Just
	 * free the data acked & wake any higher level process
	 * that was blocked waiting for space.  If the length
	 * is non-zero and the ack didn't move, we're the
	 * receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data to
	 * the socket buffer and note that we need a delayed ack.
	 */

	udt_hdr* ti = &(pkt->hdr);
	udt_snd_buf* sb = &(m_sk->snd_buf());
	udt_rcv_buf* rb = &(m_sk->rcv_buf());
	int tiflags = ti->uh_flags;

	u_long tiwin = ti->uh_win;
	if (m_state == TCPS_ESTABLISHED &&
		(tiflags & (udt_hdr::TH_SYN | udt_hdr::TH_FIN | udt_hdr::TH_RST | udt_hdr::TH_ACK)) == udt_hdr::TH_ACK &&
		ti->uh_seq == m_rcv_nxt &&
		tiwin && tiwin == m_snd_wnd &&
		m_snd_nxt == m_snd_max) 
	{
		if (ti->uh_len == 0) 
		{	//sender endpoint.
			if (seq_gt(ti->uh_ack, m_snd_una) &&
				seq_le(ti->uh_ack, m_snd_max) &&
				is_cwnd_full_open()) 
			{
				/*
				 * this is a pure ack for outstanding data.
				 */
				int acked = ti->uh_ack - m_snd_una;
				LOG_VAR_C(acked);
				drop_acked_data(ti->uh_ack);//erase acked data
				/*
				 * If all outstanding data are acked, stop
				 * retransmit timer, otherwise restart timer
				 * using current (possibly backed-off) value.
				 * If process is waiting for space,
				 * wakeup/selwakeup/signal.  If data
				 * are ready to send, let tcp_output
				 * decide between more output or persist.
				 */
				reset_rexmt_timer();
				
				m_flags |= TF_NOTIFY_WRITE;
				if (sb->cc())
					output();
				return true;
			}
			else
			{
				return false;
			}
		} 
		else if (ti->uh_ack == m_snd_una &&
			rb->reasm_pkts().empty() &&
			ti->uh_len <= rb->acc()) 
		{
			/*
			 * this is a pure, in-sequence data packet
			 * with nothing on the reassembly queue and
			 * we have enough buffer space to take it.
			 */
			
			if(!rb->append(pkt))
			{
				BOOST_ASSERT(false);
				m_flags |= TF_ACKNOW | TF_NOTIFY_READ;
				return false;
			}
			m_rcv_nxt += ti->uh_len;
			LOG_VAR_C(m_rcv_nxt);
			m_flags |= TF_DELACK | TF_NOTIFY_READ | TF_PACKET_CONSUMED;
			return true;
		}
	}
	return false;
}

bool udt_cb::drop_out_of_rcv_wnd_packet(udt_packet* pkt)
{
	udt_hdr* ti = &(pkt->hdr);
	int win = m_sk->rcv_buf().acc();
	if (win < 0)
		win = 0;
	m_rcv_wnd = std::max(win, (int)(m_rcv_adv - m_rcv_nxt));
	
	int todrop = m_rcv_nxt - ti->uh_seq;
	if (ti->uh_flags & udt_hdr::TH_SYN)
	{
		todrop--;
	}
	if (todrop > 0) 
	{
		LOG_VAR_C(m_rcv_nxt);
		LOG_VAR_C(ti->uh_seq);
		m_flags |= TF_ACKNOW;
		return true;
	}

	todrop = (ti->uh_seq + ti->uh_len) - (m_rcv_nxt + m_rcv_wnd);
	if (todrop > 0)
	{
		m_flags |= TF_ACKNOW;
		return true;
	}
	return false;
}

bool udt_cb::can_fast_rexmt()
{
	bool can = (m_dupacks == s_tcprexmtthresh);
	return can;
}

bool udt_cb::handle_syn(udt_packet* pkt)
{
	LOG_VAR_C(m_state);
	udt_hdr* ti = &(pkt->hdr);
	ti->uh_seq++;	//syn account for 1 sequence number.
	m_flags |= TF_ACKNOW;
	
	m_snd_wnd = ti->uh_win;
	m_snd_wl1 = ti->uh_seq - 1;
	m_snd_wl2 = ti->uh_ack - 1;
	switch(m_state)
	{
	case TCPS_LISTEN:	//no break, passive open.
		{
			m_iss = s_iss;	//set initial send sequence number to global value
			tcp_sendseqinit();	
		}
	case TCPS_SYN_SENT:			//handle simultaneously open.
	case TCPS_SYN_RECEIVED:		//received multiple syn, just ack
		{
			m_irs = ti->uh_seq;		//set initial receive sequence number to peer seq number.
			tcp_rcvseqinit();
			m_state = TCPS_SYN_RECEIVED;
			m_timers[TCPT_KEEP] = TCPTV_KEEP_INIT;
			break;
		}
	case TCPS_ESTABLISHED:
		{
			m_timers[TCPT_KEEP] = TCPTV_KEEP_INIT;
			break;
		}
	default:
		{
			BOOST_ASSERT(false);
			return true;
		}
	}
	LOG_VAR_C(m_state);
	return false;
}

void udt_cb::do_fast_rexmt(udt_seq ack)
{
	LOG_FUNC_SCOPE_C();
	udt_seq old_nxt = m_snd_nxt;
	handle_packet_lost();
	u_long old_snd_cwnd = m_snd_cwnd;

	m_timers[TCPT_REXMT] = 0;
	m_snd_nxt = ack;
	m_snd_cwnd = bytes_inflight() + m_maxseg;
	
	validate_first_pkt();
	output();
	m_snd_cwnd = old_snd_cwnd;	//enter fast recovery.
	m_snd_nxt = old_nxt;
}

void udt_cb::handle_packet_lost()
{
	m_tstamp_first = m_tstamp_second;
	m_tstamp_second = m_tstamp_third;
	m_tstamp_third = GetTickCount();
	if (is_pkt_lost_rate_incr())//包丢失率增加
	{
		decr_snd_cwnd();
	}
}

void udt_cb::update_ack_state()
{
	LOG_VAR_C(m_state);
	switch (m_state)
	{
	case TCPS_SYN_SENT:
		{
			m_state = TCPS_SYN_RECEIVED;
			m_flags |= TF_ACKNOW;
			break;
		}
	case TCPS_SYN_RECEIVED:
		{
			m_state = TCPS_ESTABLISHED;
			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (m_rtt)
				xmit_timer(m_rtt);
			m_sk->on_connected();
			break;
		}
	default:
		{
			break;
		}
	}
	if (m_flags & TF_SENTFIN)	//ack to fin.
	{
		switch(m_state)
		{
		case TCPS_FIN_WAIT_1:
			{
				m_state = TCPS_FIN_WAIT_2;
				break;
			}
		case TCPS_CLOSING:
			{
				m_state = TCPS_TIME_WAIT;
				canceltimers();
				m_timers[TCPT_2MSL] = 2 * TCPTV_MSL;
				m_flags |= TF_NOTIFY_DROP;
				break;
			}
		case TCPS_LAST_ACK:
			{
				m_state = TCPS_CLOSED;
				m_flags |= TF_NOTIFY_DROP;
				break;
			}
		default:
			{
				break;
			}
		}
	}
	LOG_VAR_C(m_state);
}

void udt_cb::commit_notifies()
{
	LOG_VAR_C(m_flags);

	if (m_flags & (TF_ACKNOW | TF_NEED_OUTPUT))
	{
		output();
	}

	if (m_flags & TF_NEED_RESET)
	{
		reset_peer();
		m_flags &= ~TF_NEED_RESET;
	}

	if (m_flags & TF_NOTIFY_READ)
	{
		udt_rcv_buf* rb = &(m_sk->rcv_buf());
		udt_snd_buf* sb = &(m_sk->snd_buf());
		if (rb->above_lowat() || TCPS_HAVERCVDFIN())	
		{
			m_sk->on_readable();
		}
		m_flags &= ~TF_NOTIFY_READ;
	}

	if (m_flags & TF_NOTIFY_WRITE)
	{
		m_sk->on_writable();
		m_flags &= ~TF_NOTIFY_WRITE;
	}
	
	if (m_flags & TF_NOTIFY_DROP)
	{
		drop(m_last_error);
		m_flags &= ~TF_NOTIFY_DROP;
	}
}

bool udt_cb::pkt_consumed()
{
	if ((m_flags & TF_PACKET_CONSUMED))
	{
		m_flags &= ~TF_PACKET_CONSUMED;
		return true;
	}
	else
	{
		return false;
	}
}

void check_rexmt_data(udt_snd_buf* sb, u_long rexmt_data)
{
	LOG_VAR(rexmt_data);
	for (udt_buf::list_packets_iter_t i = sb->begin(); i != sb->end(); ++i)
	{
		udt_hdr* ti = get_hdr(i);
		if (has_rexmt(ti->uh_flags))
		{
			rexmt_data -= ti->uh_len;
		}
	}
	BOOST_ASSERT(rexmt_data == 0);
}

u_long udt_cb::drop_acked_data(udt_seq ack)
{
	u_long dropped = 0;
	udt_snd_buf* sb = &(m_sk->snd_buf());
	udt_buf::list_packets_iter_t i = sb->begin();
	while(i != sb->end())
	{
		udt_hdr* ti = get_hdr(i);
		if (seq_ge(ti->uh_seq, ack))
		{
			break;
		}
		dropped += ti->uh_len;
		i = drop_snd_pkt(sb, i);
	}
#ifdef _DEBUG
	check_rexmt_data(sb, m_retrans_data);
#endif
	update_snd_una(ack);
	return dropped;
}

bool udt_cb::handle_ack(udt_packet* pkt)
{
	udt_hdr* ti = &(pkt->hdr);
	udt_seq tiack = ti->uh_ack;
	u_long tiwin = ti->uh_win;
	if (is_old_ack(tiack))
	{
		return true;	//discard silence.
	}
	if(seq_gt(tiack, m_snd_max))
	{
		LOG_WARN_C("malformed_packet");
		m_last_error = udt_error::malformed_packet;
		m_flags |= TF_NOTIFY_DROP | TF_NEED_RESET;
		return true;	//discard active.
	}

	if (is_max_ack(tiack))
	{
		update_ack_state();	//every thing acked, update state now.
	}

	if (ti->uh_off > sizeof(udt_hdr))
	{
		handle_options((char*)ti + sizeof(udt_hdr), ti->uh_off - sizeof(udt_hdr));
	}

	if (tiack == m_snd_una)//重复ack
	{
		LOG_VAR_C(tiack);
		if (tiwin != m_snd_wnd)	//对方接收窗口改变提醒
		{
			LOG_INFO_C("window update advertisement");
			update_snd_wnd(tiwin);
			m_flags |= TF_NEED_OUTPUT;
		}
		else	//乱序或丢包导致重复ack
		{
			++m_dupacks;
			LOG_VAR_C(m_dupacks);
			if (m_timers[TCPT_REXMT] == 0)
			{
				m_dupacks = 0;
			}
			else if(can_fast_rexmt())
			{
				m_rtt = 0;
				do_fast_rexmt(tiack);
				return true;
			}
			else if(m_dupacks > s_tcprexmtthresh)	//fast recovery.
			{
				m_flags |= TF_NEED_OUTPUT;
				return true;
			}
		}
	}
	else	//m_snd_una < tiack <= m_snd_max.//new data acked.
	{
		m_flags |= TF_NOTIFY_WRITE;
		LOG_VAR_C(m_dupacks);
		incr_snd_cwnd();	//open cwnd
		m_dupacks = 0;
		drop_acked_data(tiack);
		update_snd_wnd(tiwin);
		m_flags |= TF_NEED_OUTPUT;
	}
	reset_rexmt_timer();
	return false;
}

void udt_cb::handle_options(char* ptr, size_t remain_len)
{
	LOG_VAR_C(remain_len);
	int remain_len_i = remain_len;
	while (remain_len_i >= (int)sizeof(udt_option))
	{
		udt_option* opt = (udt_option*)ptr;
		LOG_VAR_C(opt->kind);
		switch (opt->kind)
		{
		case udt_option::TCPOPT_SACK:
			{
				udt_option_sack* opt_sack = (udt_option_sack*)opt;
				opt_sack->ntoh();
				size_t consumed = handle_option_sack(opt_sack);
				ptr += consumed;
				remain_len_i -= consumed;
				break;
			}
		case udt_option::TCPOPT_CWND:
			{
				udt_option_cwnd* opt_cwnd = (udt_option_cwnd*)opt;
				opt_cwnd->cwnd = ntohl(opt_cwnd->cwnd);
				size_t consumed = handle_option_cwnd(opt_cwnd);
				ptr += consumed;
				remain_len_i -= consumed;
				break;
			}
		case udt_option::TCPOPT_NOP:
			{
				ptr += sizeof(udt_option);
				remain_len_i -= sizeof(udt_option);
				break;
			}
		case udt_option::TCPOPT_EOL:
			{
				return;
			}
		default:
			{
				BOOST_ASSERT(false);
				return;
			}
		}
	}
}

size_t udt_cb::handle_option_cwnd(const udt_option_cwnd* opt_cwnd)
{
	m_pcwnd = opt_cwnd->cwnd;
	LOG_VAR_C(m_pcwnd);
	return sizeof(udt_option_cwnd);
}

udt_buf::list_packets_iter_t udt_cb::drop_snd_pkt(udt_snd_buf* sb, udt_buf::list_packets_iter_t i)
{
	udt_hdr* ti = get_hdr(i);
	if (has_rexmt(ti->uh_flags))
	{
		m_retrans_data -= ti->uh_len;
	}
	return sb->drop(i);
}

size_t udt_cb::handle_option_sack(const udt_option_sack* opt_sack)
{
	size_t size = opt_sack->size();
	LOG_VAR_C(size);
	if (size==0)
	{
		return sizeof(udt_option);
	}
	udt_snd_buf* sb = &(m_sk->snd_buf());
	udt_buf::list_packets_iter_t b = sb->begin();
	update_snd_fack(opt_sack->pair_seqs[0].second);
	for (int i = size - 1; i >= 0; i--)
	{
		const udt_option_sack::pair_seq_t& pr = opt_sack->pair_seqs[i];
		udt_seq begin_seq = pr.first;
		udt_seq end_seq = pr.second;
		LOG_VAR_C(begin_seq);
		LOG_VAR_C(end_seq);
		do
		{
			while (b != sb->end() && seq_lt(get_hdr(b)->uh_seq, begin_seq)){++b;}
			if (b == sb->end())
			{
				return sizeof(udt_option) + size * sizeof(udt_option_sack::pair_seq_t);;
			}

			//now seq_ge((*b)->hdr.uh_seq, begin_seq)
			udt_hdr* ti = get_hdr(b);
			if (seq_le(ti->uh_seq + ti->uh_len, end_seq))
			{
				LOG_VAR_C(ti->uh_seq);
				b = drop_snd_pkt(sb, b);
			}
			else	//over bounded, try next pair.
			{
				break;
			}
		}while(b != sb->end());
	}
	return sizeof(udt_option) + size * sizeof(udt_option_sack::pair_seq_t);
}

bool udt_cb::handle_fin(udt_packet* pkt)
{
	LOG_VAR_C(m_state);
	if (!TCPS_HAVERCVDFIN()) 
	{
		m_rcv_nxt++;
		m_flags |= TF_ACKNOW | TF_NOTIFY_READ;	//when the first fin received, ack immediately.
		m_last_error = udt_error::eof;	//eof received
	}
	switch (m_state)
	{
	case TCPS_ESTABLISHED:
	case TCPS_CLOSE_WAIT:	//received multiple fin.
		{
			LOG_VAR_C("passive close");
			m_state = TCPS_CLOSE_WAIT;
			break;
		}
	case TCPS_FIN_WAIT_1:	//simultaneously close.
	case TCPS_CLOSING:		//received multiple fin.
		{
			m_state = TCPS_CLOSING;
			break;
		}
	case TCPS_FIN_WAIT_2:
	case TCPS_TIME_WAIT:	//received multiple fin.
		{
			m_state = TCPS_TIME_WAIT;
			m_timers[TCPT_2MSL] = 2 * TCPTV_MSL;
			m_flags |= TF_NOTIFY_DROP;
			break;
		}
	default:
		{
			break;
		}
	}
	LOG_VAR_C(m_state);
	return false;
}

bool udt_cb::input(udt_packet* pkt, size_t bytes_received)
{
	LOG_VAR_C(m_state);

	if (bytes_received < sizeof(udt_hdr))
	{
		return false;
	}
	udt_hdr* ti = &(pkt->hdr);
	if (has_hp(ti->uh_flags))	//received hole punch packet, ignored
	{
		LOG_INFO_C("received hole punch packet, ignored");
		return false;
	}
	ti->ntoh();
	if (ti->uh_len + ti->uh_off != bytes_received)	//check packet integrity.
	{
		return false;
	}

	LOG_VAR_C((int)ti->uh_flags);
	LOG_VAR_C(ti->uh_seq);
	LOG_VAR_C(ti->uh_ack);
	m_pseq = ti->uh_seq;
	int tiflags = ti->uh_flags;

	m_idle = 0;
	m_timers[TCPT_KEEP] = s_keepidle;

	update_rtt(ti->uh_pseq);

	if (m_state == TCPS_CLOSED)
	{
		LOG_INFO_C("closed");
		if (!has_rst(tiflags))
		{
			LOG_INFO_C("reset_peer()");
			m_last_error = udt_error::connection_reset;
			m_flags |= TF_NOTIFY_DROP | TF_NEED_RESET;
		}
		goto the_end;
	}

	if (tiflags & udt_hdr::TH_RST)
	{
		m_last_error = udt_error::connection_reset;
		m_flags |= TF_NOTIFY_DROP;
		goto the_end;
	}
	
	if(drop_out_of_rcv_wnd_packet(pkt))
	{
		LOG_INFO_C("drop_out_of_rcv_wnd_packet return");
		goto the_end;
	}

	if(do_header_prediction(pkt))
	{
		LOG_INFO_C("do_header_prediction return");
		goto the_end;
	}

	if (tiflags & udt_hdr::TH_SYN && handle_syn(pkt))
	{
		LOG_INFO_C("handle_syn return");
		goto the_end;
	}

	if (ti->uh_len || ti->uh_flags & (udt_hdr::TH_SYN | udt_hdr::TH_FIN))
	{
		TCP_REASS(pkt);
	}
	
	if(m_sk->rcv_buf().fin())	//test if fin flag in rcv_buf.
	{
		tiflags |= udt_hdr::TH_FIN;
	}
	else
	{
		tiflags &= ~udt_hdr::TH_FIN;
	}

	if (tiflags & udt_hdr::TH_ACK && handle_ack(pkt))
	{
		goto the_end;
	}

	if (tiflags & udt_hdr::TH_FIN && handle_fin(pkt))	//eof received
	{
		LOG_INFO_C("handle_fin return");
		goto the_end;
	}

the_end:

	commit_notifies();
	return pkt_consumed();
}

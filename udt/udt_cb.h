#pragma once

#include "udt_defs.h"
#include "timer.h"
#include "udt_buf.h"
#include "udt_log.h"

namespace boost 
{
	namespace asio
	{
		class io_service;
	}
}

class udt_socket;
struct udt_packet;
struct udt_option_sack;
struct udt_option_cwnd;

inline short TCPT_RANGESET(short value, short tvmin, short tvmax) 
{ 
	if (value < tvmin)
		value = tvmin;
	else if (value > tvmax)
		value = tvmax;
	return value;
}

class udt_cb
{
public:
	enum state_t
	{
		TCPS_CLOSED		=	0,	/* closed */
		TCPS_LISTEN		=	1,	/* listening for connection */
		TCPS_SYN_SENT	=	2,	/* active, have sent syn */
		TCPS_SYN_RECEIVED=	3,	/* have send and received syn */
			/* states < TCPS_ESTABLISHED are those where connections not established */
		TCPS_ESTABLISHED=	4,	/* established */
		TCPS_CLOSE_WAIT	=	5,	/* rcvd fin, waiting for close */
			/* states > TCPS_CLOSE_WAIT are those where user has closed */
		TCPS_FIN_WAIT_1	=	6,	/* have closed, sent fin */
		TCPS_CLOSING	=	7,	/* closed xchd FIN; await FIN ACK */
		TCPS_LAST_ACK	=	8,	/* had fin and close; await FIN ACK */
			/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
		TCPS_FIN_WAIT_2	=	9,	/* have closed, fin is acked */
		TCPS_TIME_WAIT	=	10,	/* in 2*msl quiet wait after close */

		TCP_NSTATES = 11,

		/*
			for active open:
			TCPS_CLOSED  --send syn(call connect)--  TCPS_SYN_SENT  --received syn|ack, send ack--  TCPS_ESTABLISHED

			for passive open:
			TCPS_CLOSED  --(call listen)--  CPS_LISTEN  --received syn, send syn|ack--  TCPS_SYN_RECEIVED  --received ack--  TCPS_ESTABLISHED


			for active close:
			TCPS_ESTABLISHED  --send fin(call close)--  TCPS_FIN_SENT  --received fin|ack, send ack--  TCPS_CLOSED

			for passive close:
			TCPS_ESTABLISHED  --received fin, send fin|ack--  TCPS_FIN_RECEIVED  --received ack or TCPT_FINACK timeout--  TCPS_CLOSED
		*/
	};

	enum timer_indices_t
	{
		TCPT_REXMT = 0,
		TCPT_PERSIST = 1,
		TCPT_KEEP = 2,
		TCPT_2MSL = 3,
		TCPT_NTIMERS,
	};

	enum parameter_t
	{
		PR_SLOWHZ = 2,	//500ms = 2Hz
		TCPTV_MSL = 30*PR_SLOWHZ,	/* max seg lifetime (hah!) */
		TCPTV_SRTTBASE = 0,			/* base roundtrip time;
						   if 0, no idea yet */
		TCPTV_SRTTDFLT= 3 * PR_SLOWHZ,	/* assumed RTT if no info */

		TCPTV_PERSMIN = 5*PR_SLOWHZ,		/* retransmit persistance */
		TCPTV_PERSMAX = 60*PR_SLOWHZ,		/* maximum persist interval */

		TCPTV_KEEP_INIT = 75*PR_SLOWHZ,		/* initial connect keep alive */
		TCPTV_KEEP_IDLE = 120 * 60 * PR_SLOWHZ,	/* dflt time before probing */
		TCPTV_KEEPINTVL = 75*PR_SLOWHZ,		/* default probe interval */
		TCPTV_KEEPCNT = 8,			/* max probes before drop */

		TCPTV_MIN = 1*PR_SLOWHZ,		/* minimum allowable value */
		TCPTV_REXMTMAX = 16*PR_SLOWHZ,		/* max allowable REXMT value */

		TCP_LINGERTIME = 120,			/* linger at most 2 minutes */

		TCP_MAXRXTSHIFT = 12,			/* maximum retransmits */

		/*
		 * The smoothed round-trip time and estimated variance
		 * are stored as fixed point numbers scaled by the values below.
		 * For convenience, these scales are also used in smoothing the average
		 * (smoothed = (1/scale)sample + ((scale-1)/scale)smoothed).
		 * With these scales, srtt has 3 bits to the right of the binary point,
		 * and thus an "ALPHA" of 0.875.  rttvar has 2 bits to the right of the
		 * binary point, and is smoothed with an ALPHA of 0.75.
		 */
		TCP_RTT_SCALE = 8,	/* multiplier for srtt; 3 bits frac. */
		TCP_RTT_SHIFT = 3,	/* shift for srtt; 3 bits frac. */
		TCP_RTTVAR_SCALE = 4,	/* multiplier for rttvar; 2 bits */
		TCP_RTTVAR_SHIFT = 2,	/* multiplier for rttvar; 2 bits */

		/*
		* Default maximum segment size for TCP.
		* With an IP MSS of 576, this is 536,
		* but 512 is probably more convenient.
		* This should be defined as MIN(512, IP_MSS - sizeof (struct tcpiphdr)).
		*/
		TCP_MSS	= 512,

		TCP_MAXWIN = 65535,	/* largest value for (unscaled) window */
		TCP_MAXCWIN = TCP_MAXWIN,

		TCP_MAX_WINSHIFT = 14,	/* maximum window shift */
		TCP_THIN_CWND_SEGS = 4,
		TCP_INFINITE_SSTHRESH = 0x7fffffff,
	};

	enum flags_t
	{
		TF_ACKNOW = 0x0001,		/* ack peer immediately */
		TF_DELACK = 0x0002,		/* ack, but try to delay it */
		TF_NODELAY = 0x0004,		/* don't delay packets to coalesce */
		TF_SENTFIN = 0x0010,		/* have sent FIN */
		TF_NOTIFY_READ = TF_SENTFIN << 1,
		TF_NOTIFY_WRITE = TF_NOTIFY_READ << 1,
		TF_PACKET_CONSUMED = TF_NOTIFY_WRITE << 1,
		TF_NOTIFY_DROP = TF_PACKET_CONSUMED << 1,
		TF_NEED_OUTPUT = TF_NOTIFY_DROP << 1,
		TF_NEED_RESET = TF_NEED_OUTPUT << 1,
	};

	udt_cb(udt_socket* sk);

	void init();

	~udt_cb();

	size_t output();

	udt_packet* get_snd_pkt(size_t len);

	void validate_first_pkt();

	u_long can_send_bytes();

	size_t compose_options(char* ptr, size_t len);

	size_t compose_cwnd_option(char* ptr, size_t len);

	size_t compose_sack_option(char* ptr, size_t len);

	void udp_output(const udt_hdr* hdr, size_t hdr_len, const void* data, size_t data_len);

	bool input(udt_packet* pkt, size_t bytes_received);

	void xmit_timer(short rtt);

	void enter_persist();

	int connect();

	int accept();

	int close();

	int shutdown();

	void drop(const boost::system::error_code& e);

	void respond(udt_seq ack, udt_seq seq, int flags);

	void canceltimers();

	inline short TCP_REXMTVAL()
	{
		return (m_srtt >> TCP_RTT_SHIFT) + m_rttvar;
	}

	inline bool TCPS_HAVERCVDFIN()
	{
		return	m_state == TCPS_CLOSE_WAIT	||
				m_state == TCPS_TIME_WAIT	||
				m_state == TCPS_CLOSING;
	}

	inline bool TCPS_HAVESNDFIN()
	{
		return (m_flags & TF_SENTFIN) != 0;
	}

	inline bool TCPS_HAVERCVDSYN()
	{
		return m_state >= TCPS_SYN_RECEIVED;
	}

	inline void tcp_sendseqinit()
	{
		m_snd_fack = m_snd_una = m_snd_nxt = m_snd_max = m_iss;
	}

	/*
	 * Macros to initialize tcp sequence numbers for
	 * send and receive from initial send and receive
	 * sequence numbers.
	 */
	inline void tcp_rcvseqinit()
	{
		m_rcv_adv = m_rcv_nxt = m_irs;
	}

	inline bool is_cwnd_full_open() const
	{
		return m_snd_cwnd >= m_snd_wnd;
	}

	inline long remain_snd_wnd() const
	{
		long remain = m_snd_wnd - (m_snd_nxt - m_snd_una);
		if (remain < 0)
		{
			remain = 0;
		}
		return remain;
	}
	
	inline bool is_old_ack(udt_seq ack)
	{
		return seq_lt(ack, m_snd_una);
	}

	void handle_packet_lost();

	inline bool is_pkt_lost_rate_incr()
	{
		u_long delta0 = m_tstamp_second - m_tstamp_first;
		u_long delta1 = m_tstamp_third - m_tstamp_second;
		bool ret =  delta1 + (delta1 >> 2) < delta0;
		return ret;
	}

	inline bool is_peer_thin_stream()
	{
		return m_pcwnd != 0 && m_pcwnd < (u_long)TCP_THIN_CWND_SEGS * m_maxseg;
	}

	inline void slow_start()
	{
		if (is_pkt_lost_rate_incr())
		{
			m_snd_cwnd = min_cwnd() + bytes_inflight();
		}
	}

	inline void incr_snd_cwnd()
	{
		u_int incr = m_maxseg;
		if (is_pkt_lost_rate_incr())
		{
			incr = incr * incr / m_snd_cwnd;
			if (incr == 0)
			{
				incr = 1;
			}
		}
		m_snd_cwnd = std::min((u_int)m_snd_cwnd + incr, (u_int)udt_cb::TCP_MAXCWIN);
	}

	inline void decr_snd_cwnd()
	{
		m_snd_cwnd -= (m_snd_cwnd >> 2);
		if (m_snd_cwnd < min_cwnd())
		{
			m_snd_cwnd = min_cwnd();
		}
	}

	inline u_long min_cwnd()
	{
		return (m_maxseg << 3);
	}

	inline bool is_valid_ack(udt_seq ack)
	{
		return seq_le(m_snd_una, ack) && seq_le(ack ,m_snd_max);
	}

	inline bool is_max_ack(udt_seq ack)
	{
		return m_snd_max == ack;
	}

	inline void update_rtt(udt_seq ack)
	{
		if (m_rtt && ack == m_rtseq)
		{
			xmit_timer(m_rtt);
		}
	}

	inline void update_snd_una(udt_seq ack)
	{
		m_snd_una = ack;
		if (seq_lt(m_snd_nxt, ack))
		{
			m_snd_nxt = ack;
		}
		update_snd_fack(ack);
	}

	inline void update_snd_fack(udt_seq ack)
	{
		if (seq_lt(m_snd_fack, ack))
		{
			m_snd_fack = ack;
		}
	}

	/* This determines how many packets are "in the network" to the best
	 * of our knowledge.  In many cases it is conservative, but where
	 * detailed information is available from the receiver (via SACK
	 * blocks etc.) we can make more aggressive calculations.
	 *
	 * Use this for decisions involving congestion control, use just
	 * tp->packets_out to determine if the send queue is empty or not.
	 *
	 * Read this equation as:
	 *
	 *	"Packets sent once on transmission queue" MINUS
	 *	"Packets left network, but not honestly ACKed yet" PLUS
	 *	"Packets fast retransmitted"
	 */

	/* Linux NewReno/SACK/FACK/ECN state machine.
	 * --------------------------------------
	 *
	 * "Open"	Normal state, no dubious events, fast path.
	 * "Disorder"   In all the respects it is "Open",
	 *		but requires a bit more attention. It is entered when
	 *		we see some SACKs or dupacks. It is split of "Open"
	 *		mainly to move some processing from fast path to slow one.
	 * "CWR"	CWND was reduced due to some Congestion Notification event.
	 *		It can be ECN, ICMP source quench, local device congestion.
	 * "Recovery"	CWND was reduced, we are fast-retransmitting.
	 * "Loss"	CWND was reduced due to RTO timeout or SACK reneging.
	 *
	 * tcp_fastretrans_alert() is entered:
	 * - each incoming ACK, if state is not "Open"
	 * - when arrived ACK is unusual, namely:
	 *	* SACK
	 *	* Duplicate ACK.
	 *	* ECN ECE.
	 *
	 * Counting packets in flight is pretty simple.
	 *
	 *	in_flight = packets_out - left_out + retrans_out
	 *
	 *	packets_out is SND.NXT-SND.UNA counted in packets.
	 *
	 *	retrans_out is number of retransmitted segments.
	 *
	 *	left_out is number of segments left network, but not ACKed yet.
	 *
	 *		left_out = sacked_out + lost_out
	 *
	 *     sacked_out: Packets, which arrived to receiver out of order
	 *		   and hence not ACKed. With SACKs this number is simply
	 *		   amount of SACKed data. Even without SACKs
	 *		   it is easy to give pretty reliable estimate of this number,
	 *		   counting duplicate ACKs.
	 *
	 *       lost_out: Packets lost by network. TCP has no explicit
	 *		   "loss notification" feedback from network (for now).
	 *		   It means that this number can be only _guessed_.
	 *		   Actually, it is the heuristics to predict lossage that
	 *		   distinguishes different algorithms.
	 *
	 *	F.e. after RTO, when all the queue is considered as lost,
	 *	lost_out = packets_out and in_flight = retrans_out.
	 *
	 *		Essentially, we have now two algorithms counting
	 *		lost packets.
	 *
	 *		FACK: It is the simplest heuristics. As soon as we decided
	 *		that something is lost, we decide that _all_ not SACKed
	 *		packets until the most forward SACK are lost. I.e.
	 *		lost_out = fackets_out - sacked_out and left_out = fackets_out.
	 *		It is absolutely correct estimate, if network does not reorder
	 *		packets. And it loses any connection to reality when reordering
	 *		takes place. We use FACK by default until reordering
	 *		is suspected on the path to this destination.
	 *
	 *		NewReno: when Recovery is entered, we assume that one segment
	 *		is lost (classic Reno). While we are in Recovery and
	 *		a partial ACK arrives, we assume that one more packet
	 *		is lost (NewReno). This heuristics are the same in NewReno
	 *		and SACK.
	 *
	 *  Imagine, that's all! Forget about all this shamanism about CWND inflation
	 *  deflation etc. CWND is real congestion window, never inflated, changes
	 *  only according to classic VJ rules.
	 *
	 * Really tricky (and requiring careful tuning) part of algorithm
	 * is hidden in functions tcp_time_to_recover() and tcp_xmit_retransmit_queue().
	 * The first determines the moment _when_ we should reduce CWND and,
	 * hence, slow down forward transmission. In fact, it determines the moment
	 * when we decide that hole is caused by loss, rather than by a reorder.
	 *
	 * tcp_xmit_retransmit_queue() decides, _what_ we should retransmit to fill
	 * holes, caused by lost packets.
	 *
	 * And the most logically complicated part of algorithm is undo
	 * heuristics. We detect false retransmits due to both too early
	 * fast retransmit (reordering) and underestimated RTO, analyzing
	 * timestamps and D-SACKs. When we detect that some segments were
	 * retransmitted by mistake and CWND reduction was wrong, we undo
	 * window reduction and abort recovery phase. This logic is hidden
	 * inside several functions named tcp_try_undo_<something>.
	 */

	/* This function decides, when we should leave Disordered state
	 * and enter Recovery phase, reducing congestion window.
	 *
	 * Main question: may we further continue forward transmission
	 * with the same cwnd?
	 */
	inline u_long bytes_inflight()	//actual quantity of data outstanding in the network.
	{
		long t = m_snd_nxt - m_snd_fack + m_retrans_data;	//in_flight = packets_out - left_out + retrans_out
		if (t < 0)
		{
			t = 0;
		}
		return (u_long)t;
	}

	inline void update_snd_wnd(u_long win)
	{
		m_snd_wnd = win;
		if (win > m_max_sndwnd)
		{
			m_max_sndwnd = win;
		}
	}

	inline void reset_rexmt_timer()
	{
		/*
		 * If all outstanding data is acked, stop retransmit
		 * timer and remember to restart (more output or persist).
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		if (m_snd_una == m_snd_max)
		{
			m_timers[TCPT_REXMT] = 0;
			m_flags |= TF_NEED_OUTPUT;
		}
		else if (m_timers[TCPT_PERSIST] == 0)
			m_timers[TCPT_REXMT] = m_rxtcur;
	}

	inline short rexmt_timer_value()
	{
		short rexmt = TCP_REXMTVAL();
		rexmt *= s_tcp_backoff[m_rxtshift];
		return TCPT_RANGESET(rexmt, m_rttmin, TCPTV_REXMTMAX);
	}

	inline void reset_peer()
	{
		respond(m_rcv_nxt, m_snd_nxt, udt_hdr::TH_RST);
	}

	inline void set_mtu(size_t mtu)
	{
		if (udt_hdr::MAX_ALL_HEADERS_SIZE <= mtu)
		{
			m_maxseg = mtu - udt_hdr::MAX_ALL_HEADERS_SIZE;
		}
	}

	bool can_persist();

	inline void leave_persist()
	{
		LOG_FUNC_SCOPE_C();
		if (m_timers[TCPT_PERSIST])
		{
			m_timers[TCPT_PERSIST] = 0;	//leave persist state.
			m_rxtshift = 0;
		}
	}

	void commit_notifies();

	bool pkt_consumed();
	
	u_long drop_acked_data(udt_seq ack);

	inline const boost::system::error_code& get_lasterror() const{return m_last_error;}

	udt_packet* tcp_template();
	
	void TCP_REASS(udt_packet* pkt);

	void tcp_reass(udt_packet* pkt);

	void merge_reasm_pkts();

	bool do_header_prediction(udt_packet* pkt);

	bool drop_out_of_rcv_wnd_packet(udt_packet* pkt);

	bool can_fast_rexmt();

	bool handle_syn(udt_packet* pkt);

	bool handle_ack(udt_packet* pkt);

	void handle_options(char* ptr, size_t remain_len);

	size_t handle_option_sack(const udt_option_sack* opt_sack);

	udt_buf::list_packets_iter_t drop_snd_pkt(udt_snd_buf* sb, udt_buf::list_packets_iter_t i);

	size_t handle_option_cwnd(const udt_option_cwnd* opt_cwnd);

	void do_fast_rexmt(udt_seq ack);

	void update_ack_state();

	bool handle_fin(udt_packet* pkt);

	static const u_char s_tcp_outflags[TCP_NSTATES];
	static size_t s_mss;		/* maximum segment size */
	static udt_seq s_iss;
	static int s_keepidle;
	static int s_keepintvl;
	static int s_maxidle;
	static int s_tcprexmtthresh;
	static int s_tcp_mssdflt;
	static int s_tcp_rttdflt;
	static const int s_tcp_backoff[TCP_MAXRXTSHIFT + 1];

protected:
#ifdef _PUBLIC
public:
#endif

	void handle_fast_timo();

	void handle_slow_timo();

	void clear_snd_buf_flags();

public:

	short	m_state;		/* state of this connection */

	char	m_force;		/* 1 if forcing out a byte */
	u_short	m_maxseg;		/* maximum segment size */
	u_short	m_flags;

	/*
	 * The following fields are used as in the protocol specification.
	 * See RFC783, Dec. 1981, page 21.
	*/
	/* send sequence variables */
	udt_seq	m_snd_una;		/* send unacknowledged */
	udt_seq	m_snd_nxt;		/* send next */
	udt_seq m_snd_fack;		//fack algorithm, most forwarded seq receiver hold, which means number of segments left network, but not ACKed yet.
	u_long m_retrans_data;	//fack algorithm, retransmitted data in network.
	udt_seq	m_iss;			/* initial send sequence number */
	udt_seq m_pseq;		//last seq has received.

/* receive sequence variables */
	u_long	m_rcv_wnd;		/* receive window */
	udt_seq	m_rcv_nxt;		/* receive next */
	udt_seq	m_irs;			/* initial receive sequence number */
/*
 * Additional variables for this implementation.
 */
/* receive variables */
	udt_seq	m_rcv_adv;		/* advertised window */
/* retransmit variables */
	udt_seq	m_snd_max;		/* highest sequence number sent;
					 * used to recognize retransmits
					 */
	short	m_rxtshift;		/* log(2) of rexmt exp. backoff */
	short	m_rxtcur;		/* current retransmit value */
	/* congestion control (for slow start, source quench, retransmit after loss) */
	
/*
 * transmit timing stuff.  See below for scale of srtt and rttvar.
 * "Variance" is actually smoothed difference.
 */
	short	m_rtt;			/* round trip time */
	udt_seq	m_rtseq;		/* sequence number being timed */
	short	m_srtt;			/* smoothed round-trip time */
	short	m_rttvar;		/* variance in round-trip time */
	u_short	m_rttmin;		/* minimum rtt allowed */

	short	m_idle;			/* inactivity time */
	u_long m_tick;

	short m_timers[TCPT_NTIMERS];	/* tcp timers */

	timer m_fast_timer;		//200ms

	timer m_slow_timer;		//500ms

	udt_seq	m_snd_wl1;		/* window update seg seq number */
	udt_seq	m_snd_wl2;		/* window update seg ack number */
	u_long	m_snd_wnd;		/* send window */
	short	m_dupacks;		/* consecutive dup acks recd */
	u_long	m_snd_cwnd;		/* congestion-controlled window */
							/* snd_cwnd size threshhold for
							 * for slow start exponential to
							 * linear switch
							 */
	u_long m_pcwnd;			// peer's congestion window.
	u_long m_max_sndwnd;		/* largest window peer has offered */
	
	u_long m_tstamp_first;	//
	u_long m_tstamp_second;	//
	u_long m_tstamp_third;	//

	udt_socket* m_sk;
	boost::system::error_code m_last_error;
	friend class udt_socket;
};

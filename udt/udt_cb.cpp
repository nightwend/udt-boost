#include "udt_cb.h"
#include "udt_socket.h"
#include "udt_mtu.h"
#include <boost/bind.hpp>
#include <boost/assert.hpp>

 /*
 * Flags used when sending segments in tcp_output.
 * Basic flags (TH_RST,TH_ACK,TH_SYN,TH_FIN) are totally
 * determined by state, with the proviso that TH_FIN is sent only
 * if all data queued for output is included in the segment.
 */
const u_char udt_cb::s_tcp_outflags[TCP_NSTATES] = 
{
	udt_hdr::TH_RST|udt_hdr::TH_ACK,//TCPS_CLOSED
	0,								//TCPS_LISTEN
	udt_hdr::TH_SYN,				//TCPS_SYN_SENT
	udt_hdr::TH_SYN|udt_hdr::TH_ACK,//TCPS_SYN_RECEIVED
	udt_hdr::TH_ACK,				//TCPS_ESTABLISHED
	udt_hdr::TH_ACK,				//TCPS_CLOSE_WAIT
	udt_hdr::TH_FIN|udt_hdr::TH_ACK,//TCPS_FIN_WAIT_1
	udt_hdr::TH_FIN|udt_hdr::TH_ACK,//TCPS_CLOSING
	udt_hdr::TH_FIN|udt_hdr::TH_ACK,//TCPS_LAST_ACK
	udt_hdr::TH_ACK,				//TCPS_FIN_WAIT_2
	udt_hdr::TH_ACK					//TCPS_TIME_WAIT
};

size_t udt_cb::s_mss = udt_mtu::DEFAULT_MTU_SIZE_BYTE - udt_hdr::MAX_ALL_HEADERS_SIZE;
udt_seq udt_cb::s_iss = 0;
int udt_cb::s_keepidle = udt_cb::TCPTV_KEEP_IDLE;
int udt_cb::s_keepintvl = udt_cb::TCPTV_KEEPINTVL;
int udt_cb::s_maxidle = udt_cb::TCPTV_KEEPCNT * udt_cb::s_keepintvl;
int udt_cb::s_tcprexmtthresh = 3;
/* patchable/settable parameters for tcp */
int udt_cb::s_tcp_mssdflt = udt_cb::TCP_MSS;
int udt_cb::s_tcp_rttdflt = udt_cb::TCPTV_SRTTDFLT / udt_cb::PR_SLOWHZ;

const int udt_cb::s_tcp_backoff[udt_cb::TCP_MAXRXTSHIFT + 1] =
{ 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

udt_cb::udt_cb(udt_socket* sk):
m_sk(sk),
m_fast_timer(sk->get_io_service()),
m_slow_timer(sk->get_io_service())
{
	init();
}

void udt_cb::init()
{
	m_last_error.clear();
	m_state = TCPS_CLOSED;		/* state of this connection */

	m_force = false;		/* 1 if forcing out a byte */
	m_maxseg = s_mss;		/* maximum segment size */
	m_flags = 0;

	/*
 * The following fields are used as in the protocol specification.
 * See RFC783, Dec. 1981, page 21.
 */
	/* send sequence variables */
	m_snd_una = 0;		/* send unacknowledged */
	m_snd_nxt = 0;		/* send next */
	
	m_iss = 0;			/* initial send sequence number */
	m_pseq = 0;
/* receive sequence variables */
	m_rcv_wnd = 0;		/* receive window */
	m_rcv_nxt = 0;		/* receive next */
	m_irs = 0;			/* initial receive sequence number */
/*
 * Additional variables for this implementation.
 */
/* receive variables */
	m_rcv_adv = 0;		/* advertised window */
/* retransmit variables */
	m_snd_max = 0;		/* highest sequence number sent;
					 * used to recognize retransmits
					 */
	m_rxtshift = 0;		/* log(2) of rexmt exp. backoff */
	m_rxtcur = TCPT_RANGESET(((TCPTV_SRTTBASE >> 2) + (TCPTV_SRTTDFLT << 1)) >> 1, 
		TCPTV_MIN,
		TCPTV_REXMTMAX);		/* current retransmit value */
	/* congestion control (for slow start, source quench, retransmit after loss) */
	
	/*
	 * transmit timing stuff.  See below for scale of srtt and rttvar.
	 * "Variance" is actually smoothed difference.
	 */
	m_rtt = 0;			/* round trip time */
	m_rtseq = 0;		/* sequence number being timed */
	m_srtt = TCPTV_SRTTBASE;			/* smoothed round-trip time */
	m_rttvar = s_tcp_rttdflt * PR_SLOWHZ << 1;		/* variance in round-trip time */
	m_rttmin = TCPTV_MIN;		/* minimum rtt allowed */

	m_idle = 0;			/* inactivity time */
	m_tick = 0;

	m_snd_wl1 = 0;		/* window update seg seq number */
	m_snd_wl2 = 0;		/* window update seg ack number */
	m_snd_wnd = min_cwnd();		/* send window */
	m_dupacks = 0;		/* consecutive dup acks recd */
	m_snd_cwnd = min_cwnd();		/* congestion-controlled window */
					/* snd_cwnd size threshhold for
					 * for slow start exponential to
					 * linear switch
					 */
	m_retrans_data = 0;
	m_pcwnd = 0;
	m_max_sndwnd = 0;

	m_tstamp_first = 0;	//
	m_tstamp_second = 0;	//
	m_tstamp_third = 0;	//
	
	canceltimers();
	
	m_slow_timer.start(500, boost::bind(&udt_cb::handle_slow_timo, this));
	m_fast_timer.start(200, boost::bind(&udt_cb::handle_fast_timo, this));
}

udt_cb::~udt_cb()
{
	close();
}

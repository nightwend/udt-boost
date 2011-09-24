#pragma once

/*
	Author:	gongyiling@myhada.com
	Data:	2011-6-21
	path mtu discovery, open you firewall to let icmp in if you want it to work properly.
*/

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/icmp.hpp>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include "tracker.h"

namespace boost
{
	namespace asio
	{
		class io_service;
	}
}

/*
 * Structure of an icmp header.
 */
struct icmp {
	u_char	icmp_type;		/* type of message, see below */
	u_char	icmp_code;		/* type sub code */
	u_short	icmp_cksum;		/* ones complement cksum of struct */
	union {
		u_char ih_pptr;			/* ICMP_PARAMPROB */
		struct in_addr ih_gwaddr;	/* ICMP_REDIRECT */
		struct ih_idseq {
			u_short	icd_id;
			u_short	icd_seq;
		} ih_idseq;
		int ih_void;

		/* ICMP_UNREACH_NEEDFRAG -- Path MTU Discovery (RFC1191) */
		struct ih_pmtu {
			u_short ipm_void;    
			u_short ipm_nextmtu;
		} ih_pmtu;
	} icmp_hun;
#define	icmp_pptr	icmp_hun.ih_pptr
#define	icmp_gwaddr	icmp_hun.ih_gwaddr
#define	icmp_id		icmp_hun.ih_idseq.icd_id
#define	icmp_seq	icmp_hun.ih_idseq.icd_seq
#define	icmp_void	icmp_hun.ih_void
#define	icmp_pmvoid	icmp_hun.ih_pmtu.ipm_void
#define	icmp_nextmtu	icmp_hun.ih_pmtu.ipm_nextmtu
};

/*
 * Definition of type and code field values.
 */
#define		ICMP_ECHOREPLY		0		/* echo reply */
#define		ICMP_UNREACH		3		/* dest unreachable, codes: */
#define		ICMP_UNREACH_NET	0		/* bad net */
#define		ICMP_UNREACH_HOST	1		/* bad host */
#define		ICMP_UNREACH_PROTOCOL	2		/* bad protocol */
#define		ICMP_UNREACH_PORT	3		/* bad port */
#define		ICMP_UNREACH_NEEDFRAG	4		/* IP_DF caused drop */
#define		ICMP_UNREACH_SRCFAIL	5		/* src route failed */
#define		ICMP_UNREACH_NET_UNKNOWN 6		/* unknown net */
#define		ICMP_UNREACH_HOST_UNKNOWN 7		/* unknown host */
#define		ICMP_UNREACH_ISOLATED	8		/* src host isolated */
#define		ICMP_UNREACH_NET_PROHIB	9		/* prohibited access */
#define		ICMP_UNREACH_HOST_PROHIB 10		/* ditto */
#define		ICMP_UNREACH_TOSNET	11		/* bad tos for net */
#define		ICMP_UNREACH_TOSHOST	12		/* bad tos for host */
#define		ICMP_SOURCEQUENCH	4		/* packet lost, slow down */
#define		ICMP_REDIRECT		5		/* shorter route, codes: */
#define		ICMP_REDIRECT_NET	0		/* for network */
#define		ICMP_REDIRECT_HOST	1		/* for host */
#define		ICMP_REDIRECT_TOSNET	2		/* for tos and net */
#define		ICMP_REDIRECT_TOSHOST	3		/* for tos and host */
#define		ICMP_ECHO		8		/* echo service */
#define		ICMP_ROUTERADVERT	9		/* router advertisement */
#define		ICMP_ROUTERSOLICIT	10		/* router solicitation */
#define		ICMP_TIMXCEED		11		/* time exceeded, code: */
#define		ICMP_TIMXCEED_INTRANS	0		/* ttl==0 in transit */
#define		ICMP_TIMXCEED_REASS	1		/* ttl==0 in reass */
#define		ICMP_PARAMPROB		12		/* ip header bad */
#define		ICMP_PARAMPROB_OPTABSENT 1		/* req. opt. absent */
#define		ICMP_TSTAMP		13		/* timestamp request */
#define		ICMP_TSTAMPREPLY	14		/* timestamp reply */
#define		ICMP_IREQ		15		/* information request */
#define		ICMP_IREQREPLY		16		/* information reply */
#define		ICMP_MASKREQ		17		/* address mask request */
#define		ICMP_MASKREPLY		18		/* address mask reply */

class udt_mtu
{
public:

	enum
	{
		IPHDR_SIZE_BYTE = 20,
		DEFAULT_TIMEOUT_MS = 1 * 1000,
		DEFAULT_MTU_SIZE_BYTE = 1006,
		MAX_MTU_SIZE_BYTE = 1500,
		DEFAULT_SEND_COUNT = 3,
	};

	typedef boost::function<void (const boost::system::error_code& e, size_t mtu)> handler_mtu_t;

	typedef boost::asio::ip::icmp protocol_t;

	typedef protocol_t::socket socket_t;

	typedef protocol_t::endpoint endpoint_t;

	udt_mtu(boost::asio::io_service& ios);

	~udt_mtu();

	int create();

	void async_probe_mtu(const endpoint_t& rep, const handler_mtu_t& handler);

	void close();

	void cancel();

	boost::asio::io_service& get_io_service(){return ios_;}

private:

	void init();

	void handle_recv(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref);

	bool prepare_data();

	void send_echo();

	void async_recv_reply();

	void async_wait();

	void handle_timeout(const boost::system::error_code& e, const tracker::weak_refer& wref);

	void notify();

	void open_socket();

private:

	boost::asio::io_service& ios_;

	boost::asio::deadline_timer timer_;

	socket_t socket_;

	handler_mtu_t handler_;

	endpoint_t remote_endpoint_;

	endpoint_t reply_endpoint_;

	std::string snd_buffer_;

	std::string rcv_buffer_;

	int current_mtu_idx_;

	size_t mtu_;

	size_t mtu_hint_;

	tracker tracker_;

	boost::system::error_code last_error_;
};

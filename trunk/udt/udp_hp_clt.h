#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-7-8
*/
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/function/function_fwd.hpp>
#include <boost/system/error_code.hpp>
#include <map>
#include <string>
#include <vector>
#include "tracker.h"
#include <algorithm>
#include "udt_defs.h"
#include "utils.h"

class udp_hp_clt
{
public:

	enum parameters
	{
		DFT_RCV_BUF_SIZE = 2048,
		DFT_SEND_PACKETS = 3,
		DFT_TIMEOUT_S = 3,
		DFT_TIMEOUT_COUNT = 20,
	};

	struct endpoint_info_t
	{
		enum state_t
		{
			TCPS_CLOSED		=	0,	/* closed */
			TCPS_LISTEN		=	1,	/* listening for connection */
			TCPS_SYN_SENT	=	2,	/* active, have sent syn */
			TCPS_SYN_RECEIVED=	3,	/* have send and received syn */
			/* states < TCPS_ESTABLISHED are those where connections not established */
			TCPS_ESTABLISHED=	4,	/* established */
		};

		endpoint_info_t(): state(TCPS_CLOSED){}
		std::string snd_buf;
		int state;
	};

	typedef std::map<udp_endpoint_t, endpoint_info_t> endpoints_info_t;

	typedef boost::function<void (const boost::system::error_code& e, const udp_endpoints_t& ep)> hole_punch_handler_t;

	udp_hp_clt(boost::asio::io_service& ios);

	~udp_hp_clt(void);

	int create(udp_socket_t* sk);

	void cancel();

	void close();

	void async_hole_punch(const hole_punch_handler_t& handler, const udp_endpoints_t& eps);

private:

	void handle_peer_resp(char* ptr, int remain_len, endpoints_info_t::iterator it);

	std::string prepare_send_peer_data(u_char flags);

	void output();

	void async_receive();

	void handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref);

	void notify();

	void async_wait(int remain_count);

	void handle_async_timeout(const boost::system::error_code& e, int remain_count, const tracker::weak_refer& wref);
	
private:

	boost::asio::io_service& ios_;

	udp_endpoint_t mapped_ep_;

	udp_endpoint_t remote_ep_;

	endpoints_info_t peer_eps_;
	
	udp_socket_t* socket_;

	hole_punch_handler_t handler_;

	boost::asio::deadline_timer timer_;

	std::string rcv_buf_;

	boost::system::error_code last_error_;

	tracker tracker_;
};


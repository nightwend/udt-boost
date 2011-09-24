#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-7-15
*/
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <string>
#include "udp_defs.h"
#include "tracker.h"

namespace boost
{
	namespace asio
	{
		class io_service;
	}
}

class udp_chknat
{

public:

	enum nat_t
	{
		nt_none=0,
		nt_fw_block,
		nt_wan,
		nt_symmetric,
		nt_full_cone,
		nt_port_restrict_cone,
		nt_restrict_cone,
	};

	enum parameters
	{
		DFT_SEND_COUNT = 5,
		DFT_RCV_BUF_SIZE = 2048,
		DFT_TIMEOUT_S = 10,
	};

	struct nat_info
	{
		nat_info():nat_type(nt_none){}
		int nat_type;
		udp_endpoint_t eps[4];	//
	};

	typedef boost::function<void (const boost::system::error_code&, const nat_info&)> chknat_handler_t;
	
	udp_chknat(boost::asio::io_service& ios);

	~udp_chknat(void);

	int create(udp_socket_t* sk);

	void async_chknat(const chknat_handler_t& handler);

	void cancel();

	void close();

private:

	void async_resolve_server(int server_idx);

	void handle_async_resolve_server(const boost::system::error_code& e, udp_protocol_t::resolver_iterator i, int server_idx, const tracker::weak_refer& wref);

	void send_request();

	void async_receive();

	void handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref);

	void async_wait();

	void handle_async_wait(const boost::system::error_code& e, const tracker::weak_refer& wref);

	void analyze();

	bool finish() const
	{
		return resp_bitmap_[0] && resp_bitmap_[1] && resp_bitmap_[2] && resp_bitmap_[3];
	}

	bool parse_endpoint(const std::string& str, udp_endpoint_t&);

	void notify();

private:

	tracker tracker_;

	boost::asio::io_service& ios_;

	udp_protocol_t::resolver resolver_;

	boost::asio::deadline_timer timer_;

	chknat_handler_t handler_;

	udp_socket_t* socket_;

	nat_info nat_info_;

	bool resp_bitmap_[4];

	static udp_endpoint_t server_eps_[2];

	char rcv_buf_[DFT_RCV_BUF_SIZE];

	udp_endpoint_t remote_ep_;

	static unsigned int s_trid;

	unsigned int trid_;

	boost::system::error_code last_error_;
};

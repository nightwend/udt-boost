#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-7-16
*/

#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "udp_defs.h"
#include "tracker.h"

namespace boost
{
	namespace asio
	{
		class io_service;
	}
}

class udp_pub_addr
{
public:

	enum parameters
	{
		DFT_TIMEOUT_S = 5,
		DFT_SEND_COUNT = 5,
		DFT_RCV_BUF_SIZE = 2048,
	};

	typedef boost::function<void (const boost::system::error_code&, const udp_endpoint_t& )> pub_addr_handler_t;

	udp_pub_addr(boost::asio::io_service& ios);

	~udp_pub_addr(void);

	int create(udp_socket_t* sk, const std::string& server_hostname, unsigned short server_port);

	void async_pub_addr(const pub_addr_handler_t& handler);

	void cancel();

	void close();

private:

	void send_request();

	void async_resolve();

	void handle_async_resolve(const boost::system::error_code& e, udp_protocol_t::resolver_iterator i, const tracker::weak_refer& wref);

	void async_receive();

	void handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref);

	void async_wait();

	void handle_async_wait(const boost::system::error_code& e, const tracker::weak_refer& wref);

	void notify();

private:

	udp_endpoint_t server_ep_;

	boost::asio::io_service& ios_;

	udp_socket_t* socket_;

	pub_addr_handler_t handler_;

	udp_endpoint_t pub_addr_ep_;

	udp_endpoint_t remote_ep_;

	boost::asio::deadline_timer timer_;

	boost::shared_ptr<udp_protocol_t::resolver> resolver_;

	char rcv_buf_[DFT_RCV_BUF_SIZE];

	tracker tracker_;

	static unsigned int s_trid_;

	unsigned int trid_;

	std::string server_hostname_;
	unsigned short server_port_;

	boost::system::error_code last_error_;
};

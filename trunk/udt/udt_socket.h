#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	before 2011-7-4
*/
#include <list>
#include <boost/asio/ip/udp.hpp>
#include <boost/function.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "udt_buf.h"
#include "udp_defs.h"
#include "tracker.h"

namespace boost
{
	namespace asio
	{
		class io_service;
	}
}

class udt_cb;
class udt_mtu;

class udt_socket	//this guy is not thread safety.
{
public:

	enum udt_socket_options_t
	{
		UDT_NONE = 0,
		UDT_KEEPALIVE = 1,
		UDT_SND_BUF = 2,
		UDT_RCV_BUF = 3,
	};

	typedef udp_socket_t next_layer_t;

	typedef boost::function<void (const boost::system::error_code&, size_t)> handler_t;

	typedef boost::function<void (const boost::system::error_code&)> connect_handler_t;

	typedef boost::function<void (const boost::system::error_code&, const udp_endpoint_t&)> accept_handler_t;

	typedef boost::function<void (const boost::system::error_code&)> shutdown_handler_t;

	typedef boost::asio::const_buffer const_buffer_t;

	typedef boost::asio::mutable_buffer mutable_buffer_t;

	struct send_operation
	{
		send_operation(){}
		send_operation(const const_buffer_t& b, const handler_t& h):buf(b), handler(h){}
		const_buffer_t buf;
		handler_t handler;
	};

	struct recv_operation 
	{
		recv_operation(){}
		recv_operation(const mutable_buffer_t& b, const handler_t& h):buf(b), handler(h){}
		mutable_buffer_t buf;
		handler_t handler;
	};

	typedef std::list<send_operation> send_ops_t;

	typedef std::list<recv_operation> recv_ops_t;

	udt_socket(boost::asio::io_service& ios);

	~udt_socket();

	void create(boost::system::error_code& e, next_layer_t* sk);	//传入boost::asio::ip::udp::socket指针，初始化udt_socket.

	void async_connect(const udp_endpoint_t& rep, const connect_handler_t& connect_handler);	//异步连接(主动连接).

	void async_accept(const udp_endpoint_t& rep, const accept_handler_t& accept_handler);		//异步接受(被动连接).

	void async_send(const const_buffer_t& cbuf, const handler_t& handler);	//异步发送数据.

	void async_receive(const mutable_buffer_t& buf, const handler_t& handler);	//异步接收数据.

	void close(boost::system::error_code& e);	//关闭UDT连接，但不关闭UDP套接字.

	void async_shutdown(const shutdown_handler_t& handler);	//关闭写套接字(发送fin到对方端口).

	void cancel(boost::system::error_code& e);	//取消未完成操作.

	int setsockopt(int optname, const void* value, int optlen);

	int getsockopt(int optname, void* value, int* optlen);	//not implemented yet.

	size_t total_buffer_size() const {return pending_data_ + snd_buf_.cc();}

	size_t pending_data() const {return pending_data_;}

private:
#ifdef _PUBLIC
public:
#endif

	boost::asio::io_service& get_io_service(){return ios_;}

	next_layer_t* next_layer(){return next_layer_;}

	udp_endpoint_t& remote_endpoint(){return remote_ep_;}

	udp_endpoint_t& peer_endpoint(){return peer_ep_;}

	udt_snd_buf& snd_buf(){return snd_buf_;}

	udt_rcv_buf& rcv_buf(){return rcv_buf_;}

	void on_readable();

	void recv_data();

	bool dispatch_recv_operation(recv_operation& ops);

	void delay_recv_operation(recv_operation& ops);

	void handle_delay_recv_operation(const boost::system::error_code& e, const tracker::weak_refer& wref);

	void on_writable();

	void on_connected();

	void on_disconnected();

	void do_probe_mtu();

	void handle_mtu(const boost::system::error_code& e, size_t mtu, const tracker::weak_refer& wref);

	size_t consume_pending_data();

	void do_receive();

	void handle_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref);

	void cancel_next_layer();

private:
#ifdef _PUBLIC
public:
#endif

	tracker tracker_;
	boost::asio::io_service& ios_;
	next_layer_t* next_layer_;
	udp_endpoint_t remote_ep_;
	udp_endpoint_t peer_ep_;

	send_ops_t send_ops_;
	udt_snd_buf snd_buf_;

	recv_ops_t recv_ops_;
	udt_rcv_buf rcv_buf_;

	connect_handler_t connect_handler_;
	accept_handler_t accept_handler_;
	shutdown_handler_t shutdown_handler_;

	udt_cb* cb_;			//control block
	udt_mtu* mtu_prober_;	//path mtu discovery, open you firewall to let icmp in if you want it to work properly.
	size_t pending_data_;
	udt_packet* rcv_pkt_;
	u_long so_options_;
	boost::asio::deadline_timer delay_copy_timer_;

	friend class udt_cb;
};

#ifdef _DEBUG

void test_udt_socket();

#endif
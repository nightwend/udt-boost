#pragma once

/*
	Author: gongyiling@myhada.com
	Date: 2011-9-15
*/
#include <boost/function/function_fwd.hpp>
#include <boost/asio/ip/udp.hpp>

struct udt_engine_socket;

namespace boost { namespace system { 
	class error_code;
} }

class iudt_engine
{
public:

	enum udt_socket_options_t
	{
		UDT_NONE = 0,
		UDT_KEEPALIVE = 1,
		UDT_SND_BUF = 2,
		UDT_RCV_BUF = 3,
	};

	typedef boost::asio::ip::udp::endpoint udp_endpoint_t;

	typedef std::vector<udp_endpoint_t> udp_endpoints_t;

	typedef boost::function<void (const boost::system::error_code&, size_t)> handler_t;

	typedef boost::function<void (const boost::system::error_code&)> connect_handler_t;

	typedef boost::function<void (const boost::system::error_code&, const udp_endpoint_t&)> accept_handler_t;

	typedef boost::function<void (const boost::system::error_code&)> shutdown_handler_t;

	typedef boost::function<void (const boost::system::error_code&, const udp_endpoint_t& )> pub_addr_handler_t;

	typedef boost::function<void (const boost::system::error_code& e, const udp_endpoints_t& ep)> hole_punch_handler_t;

	typedef boost::asio::const_buffer const_buffer_t;

	typedef boost::asio::mutable_buffer mutable_buffer_t;

	static iudt_engine* get_instance();

	static void release_instance();

	virtual void run() = 0;

	virtual void stop() = 0;

	virtual udt_engine_socket* create(unsigned short port) = 0;

	virtual void close(udt_engine_socket* sock) = 0;

	virtual void async_pub_addr(udt_engine_socket* sock,
		const std::string& server_hostname,
		unsigned short server_port,
		const pub_addr_handler_t& pub_addr_handler) = 0;

	virtual void async_hole_punch(udt_engine_socket* sock,
		const udp_endpoints_t& eps,
		const hole_punch_handler_t& handler) = 0;

	virtual void async_connect(udt_engine_socket* sock, 
		const udp_endpoint_t& rep,
		const connect_handler_t& connect_handler) = 0;	//异步连接(主动连接).

	virtual void async_accept(udt_engine_socket* sock,
		const udp_endpoint_t& rep,
		const accept_handler_t& accept_handler) = 0;		//异步接受(被动连接).

	virtual void async_send(udt_engine_socket* sock,
		const const_buffer_t& cbuf,
		const handler_t& handler) = 0;	//异步发送数据.

	virtual void async_receive(udt_engine_socket* sock,
		const mutable_buffer_t& buf,
		const handler_t& handler) = 0;	//异步接收数据.

	virtual void async_shutdown(udt_engine_socket* sock,
		const shutdown_handler_t& handler) = 0;	//关闭写套接字(发送fin到对方端口).

	virtual void cancel(udt_engine_socket* sock,
		boost::system::error_code& e) = 0;	//取消未完成操作.

	virtual int setsockopt(udt_engine_socket* sock,
		int optname, 
		const void* value,
		int optlen) = 0;

	virtual int getsockopt(udt_engine_socket* sock,
		int optname,
		void* value, 
		int* optlen) = 0;	//not implemented yet.

	virtual size_t pending_data(const udt_engine_socket* sock) = 0;
};

#pragma once

/*
	Author:	gongyiling@myhada.com
	Date:	2011-715
*/
#include <boost/asio/io_service.hpp>
#include <boost/thread/thread.hpp>
#include <set>
#include "udt/iudt_engine.h"
#include "udp_defs.h"
#include "utils.h"
#include "tracker.h"

class udt_engine : public iudt_engine
{
public:

	typedef std::set<udt_engine_socket*> udt_engine_socket_set_t;

	udt_engine(void);

	virtual ~udt_engine(void);

	virtual void run();

	virtual void stop();

	virtual udt_engine_socket* create(unsigned short port);

	virtual void close(udt_engine_socket* sock);

	virtual void async_pub_addr(udt_engine_socket* sock,
		const std::string& server_hostname,
		unsigned short server_port,
		const pub_addr_handler_t& pub_addr_handler);

	virtual void async_hole_punch(udt_engine_socket* sock,
		const udp_endpoints_t& eps,
		const hole_punch_handler_t& handler);

	virtual void async_connect(udt_engine_socket* sock, 
		const udp_endpoint_t& rep,
		const connect_handler_t& connect_handler);	//异步连接(主动连接).

	virtual void async_accept(udt_engine_socket* sock,
		const udp_endpoint_t& rep,
		const accept_handler_t& accept_handler);		//异步接受(被动连接).

	virtual void async_send(udt_engine_socket* sock,
		const const_buffer_t& cbuf,
		const handler_t& handler);	//异步发送数据.

	virtual void async_receive(udt_engine_socket* sock,
		const mutable_buffer_t& buf,
		const handler_t& handler);	//异步接收数据.

	virtual void async_shutdown(udt_engine_socket* sock,
		const shutdown_handler_t& handler);	//关闭写套接字(发送fin到对方端口).

	virtual void cancel(udt_engine_socket* sock,
		boost::system::error_code& e);	//取消未完成操作.

	virtual int setsockopt(udt_engine_socket* sock,
		int optname, 
		const void* value,
		int optlen);

	virtual int getsockopt(udt_engine_socket* sock,
		int optname,
		void* value, 
		int* optlen);	//not implemented yet.

	virtual size_t pending_data(const udt_engine_socket* sock);

private:

	udt_engine_socket* create_(unsigned short port);

	void close_(udt_engine_socket* sock);

	void async_pub_addr_(udt_engine_socket* sock,
		const std::string& server_hostname,
		unsigned short server_port,
		const pub_addr_handler_t& pub_addr_handler);

	void async_hole_punch_(udt_engine_socket* sock,
		const udp_endpoints_t& eps,
		const hole_punch_handler_t& handler);

	void async_connect_(udt_engine_socket* sock, 
		const udp_endpoint_t& rep,
		const connect_handler_t& connect_handler);

	void async_accept_(udt_engine_socket* sock,
		const udp_endpoint_t& rep,
		const accept_handler_t& accept_handler);

	void async_send_(udt_engine_socket* sock,
		const const_buffer_t& cbuf,
		const handler_t& handler);

	void async_receive_(udt_engine_socket* sock,
		const mutable_buffer_t& buf,
		const handler_t& handler);

	void async_shutdown_(udt_engine_socket* sock,
		const shutdown_handler_t& handler);

	void cancel_(udt_engine_socket* sock);

	int setsockopt_(udt_engine_socket* sock,
		int optname, 
		const void* value,
		int optlen);

	int getsockopt_(udt_engine_socket* sock,
		int optname,
		void* value, 
		int* optlen);

	size_t pending_data_(const udt_engine_socket* sock);

	void handle_async_pub_addr(const boost::system::error_code& e,
		const udp_endpoint_t& ep,
		udt_engine_socket* sock,
		const pub_addr_handler_t& handler);

	void handle_async_hole_punch(const boost::system::error_code& e, 
		const udp_endpoints_t& ep,
		udt_engine_socket* sock,
		const hole_punch_handler_t& handler);

	void thread_proc();

	void clear();

	void clear_();

	bool socket_exists(const udt_engine_socket* sock) const;

	boost::asio::io_service& get_io_service(){return ios_;}

	template<typename Handler>
	void post_handler(const Handler& handler)
	{
		post_io_service(get_io_service(), handler, tracker_.get());
	}

	template <typename Handler, typename ResultType>
	void invoke_handler(const Handler& handler, ResultType* result)
	{
		invoke_io_service<Handler, ResultType>(get_io_service(), &handler, result);
	}

	boost::asio::io_service ios_;
	boost::asio::io_service::work* worker_;

	boost::thread* thread_;

	udt_engine_socket_set_t udt_engine_socket_set_;
	tracker tracker_;
};

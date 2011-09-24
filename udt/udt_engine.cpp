#include "udt_engine.h"
#include <boost/assert.hpp>
#include <boost/bind.hpp>
#include "singleton.h"
#include "udt_socket.h"
#include "udp_pub_addr.h"
#include "udp_hp_clt.h"
#include "udt_log.h"

struct udt_engine_socket
{
	udt_engine_socket(boost::asio::io_service& ios):raw_sock(ios), sock(ios), pub_addr(NULL), hp_clt(NULL) {}
	~udt_engine_socket()
	{
		close();
	}
	udt_socket::next_layer_t raw_sock;
	udt_socket sock;
	udp_pub_addr* pub_addr;
	udp_hp_clt* hp_clt;

private:

	void close()
	{
		boost::system::error_code ignored;
		sock.close(ignored);
		raw_sock.close(ignored);
		if (pub_addr)
		{
			pub_addr->close();
			delete pub_addr;
			pub_addr = NULL;
		}
		if (hp_clt)
		{
			hp_clt->close();
			delete hp_clt;
			hp_clt = NULL;
		}
	}
};

iudt_engine* iudt_engine::get_instance()
{
	return singleton<udt_engine>::get_instance();
}

void iudt_engine::release_instance()
{
	singleton<udt_engine>::release_instance();
}

udt_engine::udt_engine(void):
thread_(NULL),
worker_(NULL)
{
}

udt_engine::~udt_engine(void)
{
	stop();
}

void udt_engine::run()
{
	BOOST_ASSERT(thread_ == NULL);
	LOG_FUNC_SCOPE_C();
	if (thread_ == NULL)
	{
		ios_.reset();
		if (worker_)
		{
			delete worker_;
		}
		worker_ = new boost::asio::io_service::work(ios_);
		thread_ = new boost::thread(boost::bind(&udt_engine::thread_proc, this));
	}
}

void udt_engine::stop()
{
	BOOST_ASSERT(thread_ != NULL);
	LOG_FUNC_SCOPE_C();
	if (thread_ != NULL)
	{
		if (worker_)
		{
			delete worker_;
			worker_ = NULL;
		}
		ios_.stop();
		thread_->join();
		delete thread_;
		thread_ = NULL;
	}
}

void udt_engine::thread_proc()
{
	boost::system::error_code ignored;
	ios_.run(ignored);
	clear();
}

void udt_engine::clear()
{
	LOG_FUNC_SCOPE_C();
	post_handler(boost::bind(&udt_engine::clear_, this));
}

void udt_engine::clear_()
{
	for (udt_engine_socket_set_t::iterator i = udt_engine_socket_set_.begin(); i != udt_engine_socket_set_.end(); ++i)
	{
		udt_engine_socket* eg_sock = *i;
		delete eg_sock;
	}
	udt_engine_socket_set_.clear();
}

inline bool udt_engine::socket_exists(const udt_engine_socket* sock) const
{
	return udt_engine_socket_set_.find(const_cast<udt_engine_socket*>(sock)) != udt_engine_socket_set_.end();
}

udt_engine_socket* udt_engine::create(unsigned short port)
{
	udt_engine_socket* sock = NULL;
	invoke_handler(boost::bind(&udt_engine::create_, this, port), &sock);
	return sock;
}

udt_engine_socket* udt_engine::create_(unsigned short port)
{
	udt_engine_socket* sock = new udt_engine_socket(ios_);
	boost::system::error_code ignored;
	sock->raw_sock.open(udp_protocol_t::v4(), ignored);
	sock->raw_sock.bind(udp_endpoint_t(boost::asio::ip::address_v4::any(), port), ignored);
	sock->sock.create(ignored, &(sock->raw_sock));
	udt_engine_socket_set_.insert(sock);
	return sock;
}

void udt_engine::close(udt_engine_socket* sock)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::close_, this, sock));
}

void udt_engine::close_(udt_engine_socket* sock)
{
	udt_engine_socket_set_t::iterator i = udt_engine_socket_set_.find(sock);
	if (i != udt_engine_socket_set_.end())
	{
		udt_engine_socket* eg_sock = *i;
		delete eg_sock;
		udt_engine_socket_set_.erase(i);
	}
}

void udt_engine::async_pub_addr(udt_engine_socket* sock,
					const std::string& server_hostname,
					unsigned short server_port,
					const pub_addr_handler_t& pub_addr_handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_pub_addr_, this, sock, server_hostname, server_port, pub_addr_handler));
}

void udt_engine::async_pub_addr_(udt_engine_socket* sock,
					 const std::string& server_hostname,
					 unsigned short server_port,
					 const pub_addr_handler_t& pub_addr_handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	if (sock->pub_addr)
	{
		delete sock->pub_addr;
	}
	sock->pub_addr = new udp_pub_addr(ios_);
	sock->pub_addr->create(&(sock->raw_sock), server_hostname, server_port);
	sock->pub_addr->async_pub_addr(boost::bind(&udt_engine::handle_async_pub_addr, this, _1, _2, sock, pub_addr_handler));
}

void udt_engine::handle_async_pub_addr(const boost::system::error_code& e,
									   const udp_endpoint_t& ep, 
									   udt_engine_socket* sock,
									   const pub_addr_handler_t& handler)
{
	if (socket_exists(sock))
	{
		delete sock->pub_addr;
		sock->pub_addr = NULL;
		handler(e, ep);
	}
}

void udt_engine::async_hole_punch(udt_engine_socket* sock,
					  const udp_endpoints_t& eps,
					  const hole_punch_handler_t& handler)

{
	post_handler(boost::bind(&udt_engine::async_hole_punch_, this, sock, eps, handler));
}

void udt_engine::async_hole_punch_(udt_engine_socket* sock,
					   const udp_endpoints_t& eps,
					   const hole_punch_handler_t& handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
	}
	if (sock->hp_clt)
	{
		delete sock->hp_clt;
	}
	sock->hp_clt = new udp_hp_clt(ios_);
	sock->hp_clt->create(&(sock->raw_sock));
	sock->hp_clt->async_hole_punch(boost::bind(&udt_engine::handle_async_hole_punch, this, _1, _2, sock, handler), eps);
}

void udt_engine::handle_async_hole_punch(const boost::system::error_code& e, 
							 const udp_endpoints_t& eps,
							 udt_engine_socket* sock,
							 const hole_punch_handler_t& handler)
{
	if (socket_exists(sock))
	{
		delete sock->hp_clt;
		sock->hp_clt = NULL;
		handler(e, eps);
	}
}

void udt_engine::async_connect(udt_engine_socket* sock, 
				   const udp_endpoint_t& rep,
				   const connect_handler_t& connect_handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_connect_, this, sock, rep, connect_handler));
}

void udt_engine::async_connect_(udt_engine_socket* sock, 
					const udp_endpoint_t& rep,
					const connect_handler_t& connect_handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	sock->sock.async_connect(rep, connect_handler);
}

void udt_engine::async_accept(udt_engine_socket* sock,
				  const udp_endpoint_t& rep,
				  const accept_handler_t& accept_handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_accept_, this, sock, rep, accept_handler));
}

void udt_engine::async_accept_(udt_engine_socket* sock,
				   const udp_endpoint_t& rep,
				   const accept_handler_t& accept_handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	sock->sock.async_accept(rep, accept_handler);
}

void udt_engine::async_send(udt_engine_socket* sock,
				const const_buffer_t& cbuf,
				const handler_t& handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_send_, this, sock, cbuf, handler));
}

void udt_engine::async_send_(udt_engine_socket* sock,
				 const const_buffer_t& cbuf,
				 const handler_t& handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	sock->sock.async_send(cbuf, handler);
}

void udt_engine::async_receive(udt_engine_socket* sock,
				   const mutable_buffer_t& buf,
				   const handler_t& handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_receive_, this, sock, buf, handler));
}

void udt_engine::async_receive_(udt_engine_socket* sock,
					const mutable_buffer_t& buf,
					const handler_t& handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	sock->sock.async_receive(buf, handler);
}

void udt_engine::async_shutdown(udt_engine_socket* sock,
					const shutdown_handler_t& handler)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::async_shutdown_, this, sock, handler));
}

void udt_engine::async_shutdown_(udt_engine_socket* sock,
					 const shutdown_handler_t& handler)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	sock->sock.async_shutdown(handler);
}

void udt_engine::cancel(udt_engine_socket* sock,
			boost::system::error_code& e)
{
	LOG_VAR_C(sock);
	post_handler(boost::bind(&udt_engine::cancel_, this, sock));
}

void udt_engine::cancel_(udt_engine_socket* sock)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return;
	}
	boost::system::error_code e;
	sock->sock.cancel(e);
}

int udt_engine::setsockopt(udt_engine_socket* sock,
			   int optname, 
			   const void* value,
			   int optlen)
{
	LOG_VAR_C(sock);
	int result = -1;
	invoke_handler(boost::bind(&udt_engine::setsockopt_, this, sock, optname, value, optlen), &result);
	return result;
}

int udt_engine::setsockopt_(udt_engine_socket* sock,
				int optname, 
				const void* value,
				int optlen)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return -1;
	}
	return sock->sock.setsockopt(optname, value, optlen);
}

int udt_engine::getsockopt(udt_engine_socket* sock,
			   int optname,
			   void* value, 
			   int* optlen)
{
	LOG_VAR_C(sock);
	int result = -1;
	invoke_handler(boost::bind(&udt_engine::getsockopt_, this, sock, optname, value, optlen), &result);
	return result;
}

int udt_engine::getsockopt_(udt_engine_socket* sock,
				int optname,
				void* value, 
				int* optlen)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return -1;
	}
	return sock->sock.getsockopt(optname, value, optlen);
}

size_t udt_engine::pending_data(const udt_engine_socket* sock)
{
	size_t result = -1;
	invoke_handler(boost::bind(&udt_engine::pending_data_, this, sock), &result);
	return result;
}

size_t udt_engine::pending_data_(const udt_engine_socket* sock)
{
	if (!socket_exists(sock))
	{
		LOG_INFO_C("socket not exists");
		return -1;
	}
	return sock->sock.pending_data();
}

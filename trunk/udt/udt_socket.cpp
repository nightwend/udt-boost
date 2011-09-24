#include "udt_socket.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/assert.hpp>
#include "udt_cb.h"
#include "udt_mtu.h"
#include "udt_log.h"
#include "udt_error.h"
#include "udt_mtu.h"

udt_socket::udt_socket(boost::asio::io_service& ios):
ios_(ios),
next_layer_(NULL),
mtu_prober_(NULL),
cb_(NULL),
pending_data_(0),
rcv_pkt_(NULL),
so_options_(UDT_NONE),
delay_copy_timer_(ios)
{
	
}

udt_socket::~udt_socket(void)
{
	boost::system::error_code e;
	close(e);
}

void udt_socket::create(boost::system::error_code& e, next_layer_t* sk)
{
	if (next_layer_ != NULL)
	{
		BOOST_ASSERT(false);
		e = udt_error::alread_created;
		return;
	}
	boost::system::error_code ignored;
	if(sk->local_endpoint(ignored) == udp_endpoint_t())	//must binded.
	{
		BOOST_ASSERT(false);
		e = udt_error::not_bind;
		return ;
	}
	so_options_ = UDT_NONE;
	next_layer_ = sk;
	cb_ = new udt_cb(this);
	mtu_prober_ = new udt_mtu(ios_);
	mtu_prober_->create();
	e = udt_error::succeed;
}

void udt_socket::async_connect(const udp_endpoint_t& rep, const connect_handler_t& connect_handler)
{
	if (next_layer_ == NULL)	//disconnected.
	{
		BOOST_ASSERT(false);
		return;
	}
	connect_handler_ = connect_handler;
	peer_endpoint() = rep;
	do_probe_mtu();
	do_receive();
	cb_->connect();
}

inline void udt_socket::do_probe_mtu()
{
	udt_mtu::endpoint_t ep(peer_endpoint().address(), peer_endpoint().port());
	mtu_prober_->async_probe_mtu(ep, boost::bind(&udt_socket::handle_mtu, this, _1, _2, tracker_.get()));
}

void udt_socket::handle_mtu(const boost::system::error_code& e, size_t mtu, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	LOG_VAR_C(mtu);
	if (wref.expired() || next_layer_ == NULL)	//disconnected.
	{
		return;
	}
	if (mtu_prober_ != NULL)
	{
		delete mtu_prober_;
		mtu_prober_ = NULL;	//nolong needed.
	}
	cb_->set_mtu(1006);
}

void udt_socket::async_accept(const udp_endpoint_t& rep, const accept_handler_t& accept_handler)
{
	LOG_FUNC_SCOPE_C();
	if (next_layer_ == NULL)	//disconnected.
	{
		BOOST_ASSERT(false);
		return;
	}
	accept_handler_ = accept_handler;
	peer_endpoint() = rep;
	do_receive();
	cb_->accept();
}

void udt_socket::async_send(const const_buffer_t& cbuf, const handler_t& handler)
{
	if (next_layer_ == NULL)
	{
		BOOST_ASSERT(false);
		return;
	}
	const char* data = (const char*)boost::asio::detail::buffer_cast_helper(cbuf);
	size_t data_len = boost::asio::detail::buffer_size_helper(cbuf);
	
	if (data_len == 0 || data == NULL || cb_->get_lasterror())
	{
		LOG_INFO_C("append to send buffer");
		get_io_service().post(boost::asio::detail::bind_handler(handler, cb_->get_lasterror(), 0));
	}
	else
	{
		LOG_INFO_C("pending operation");
		send_ops_.push_back(send_operation(cbuf, handler));	//pending until on_writable
		pending_data_ += data_len;
		if(consume_pending_data())
			cb_->output();
	}
}

void udt_socket::async_receive(const mutable_buffer_t& buf, const handler_t& handler)
{
	LOG_FUNC_SCOPE_C();
	if (next_layer_ == NULL)
	{
		BOOST_ASSERT(false);
		return;
	}
	char* data = (char*)boost::asio::detail::buffer_cast_helper(buf);
	size_t data_len = boost::asio::detail::buffer_size_helper(buf);
	
	if (data_len == 0 || data == NULL || cb_->get_lasterror())
	{
		get_io_service().post(boost::asio::detail::bind_handler(handler, cb_->get_lasterror(), 0));
	}
	else
	{
		recv_ops_.push_back(recv_operation(buf, handler));	//pending until on_readable
		recv_data();
	}
}

void udt_socket::async_shutdown(const shutdown_handler_t& handler)
{
	if (next_layer_ == NULL)
	{
		BOOST_ASSERT(false);
		return;
	}
	shutdown_handler_ = handler;
	cb_->shutdown();
}

void udt_socket::cancel(boost::system::error_code& e)
{
	cancel_next_layer();
	send_ops_.clear();
	recv_ops_.clear();
	accept_handler_.clear();
	connect_handler_.clear();
	shutdown_handler_.clear();
	pending_data_ = 0;
	e = udt_error::succeed;
}

void udt_socket::on_readable()
{
	LOG_VAR_C(recv_ops_.size());
	
	if (next_layer_ == NULL)
	{
		BOOST_ASSERT(false);
		return;
	}
	recv_data();
}

void udt_socket::recv_data()
{
	if (!rcv_buf_.cc() && !cb_->get_lasterror())
	{
		return;
	}
	bool output = false;
	while (!recv_ops_.empty())
	{
		recv_operation& ops = recv_ops_.front();
		size_t buffer_len = boost::asio::detail::buffer_size_helper(ops.buf);
		if (!cb_->get_lasterror() && buffer_len > rcv_buf_.cc())
		{
			delay_recv_operation(ops);
			break;
		}
		else if (dispatch_recv_operation(ops))
		{
			recv_ops_.pop_front();
			output = true;
		}
		else
		{
			break;
		}
	}
	if (!cb_->get_lasterror() && output)
	{
		cb_->output();
	}
}

bool udt_socket::dispatch_recv_operation(recv_operation& ops)
{
	mutable_buffer_t& buf  = ops.buf;
	handler_t& handler = ops.handler;
	char* data = (char*)boost::asio::detail::buffer_cast_helper(buf);
	size_t data_len = boost::asio::detail::buffer_size_helper(buf);
	LOG_VAR_C(rcv_buf_.cc());
	size_t copied = rcv_buf_.copy(data, data_len, true);
	LOG_VAR_C(copied);
	if (copied || cb_->get_lasterror())
	{
		get_io_service().post(boost::asio::detail::bind_handler(handler, cb_->get_lasterror(), copied));
		LOG_VAR_C(rcv_buf_.cc());
		return true;
	}
	LOG_VAR_C(rcv_buf_.cc());
	return false;
}

void udt_socket::delay_recv_operation(recv_operation& ops)
{
	delay_copy_timer_.expires_from_now(boost::posix_time::millisec(50));
	delay_copy_timer_.async_wait(boost::bind(&udt_socket::handle_delay_recv_operation, this, _1, tracker_.get()));
}

void udt_socket::handle_delay_recv_operation(const boost::system::error_code& e, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	while (!recv_ops_.empty())
	{
		if (dispatch_recv_operation(recv_ops_.front()))
		{
			recv_ops_.pop_front();
		}
		else
		{
			break;
		}
	}
}

void udt_socket::on_writable()
{
	LOG_VAR_C(send_ops_.size());
	if (next_layer_ == NULL)
	{
		BOOST_ASSERT(false);
		return;
	}
	if (consume_pending_data())
	{
		cb_->output();
	}
}

void udt_socket::on_connected()
{
	LOG_FUNC_SCOPE_C();
	if (next_layer_ == NULL)
	{
		return;
	}
	if (connect_handler_)	//active connect result.
	{
		get_io_service().post(boost::asio::detail::bind_handler(connect_handler_, 
			cb_->get_lasterror()));
		connect_handler_.clear();
	}
	else if (accept_handler_)	//passive connect result
	{
		get_io_service().post(boost::asio::detail::bind_handler(accept_handler_, 
			cb_->get_lasterror(),
			peer_endpoint()));
		accept_handler_.clear();
		do_probe_mtu();
	}
	snd_buf_.set_init_seq(cb_->m_snd_nxt);
}

void udt_socket::on_disconnected()
{
	LOG_VAR_C(next_layer_);
	if (next_layer_ == NULL)
	{
		return;
	}
	boost::system::error_code e = udt_error::disconnected;
	for (send_ops_t::iterator i = send_ops_.begin(); i != send_ops_.end(); ++i)
	{
		get_io_service().post(boost::asio::detail::bind_handler(i->handler, e, 0));
	}
	recv_data();
	if (connect_handler_)
	{
		get_io_service().post(boost::asio::detail::bind_handler(connect_handler_, e));
	}
	else if (accept_handler_)
	{
		get_io_service().post(boost::asio::detail::bind_handler(accept_handler_, e, peer_endpoint()));
	}
	if (shutdown_handler_)
	{
		get_io_service().post(boost::asio::detail::bind_handler(shutdown_handler_, e));
	}
	cancel(e);
}

void udt_socket::close(boost::system::error_code& e)	//FIXME, this guy may not thread safety.
{
	LOG_FUNC_SCOPE_C();
	if (next_layer_ == NULL)
	{
		return;
	}
	cancel(e);
	next_layer_ = NULL;
	if (mtu_prober_ != NULL)
	{
		delete mtu_prober_;
		mtu_prober_ = NULL;
	}
	if (cb_ != NULL)
	{
		delete cb_;
		cb_ = NULL;
	}
	snd_buf_.clear();
	rcv_buf_.clear();
	e = udt_error::succeed;
}

void udt_socket::cancel_next_layer()
{
	LOG_VAR_C(next_layer_);
	if (mtu_prober_ != NULL)
	{
		mtu_prober_->cancel();
	}
	if (next_layer_ != NULL)
	{
		boost::system::error_code ignored;
		next_layer()->cancel(ignored);
	}
	delay_copy_timer_.cancel();
}

int udt_socket::setsockopt(int optname, const void* value, int optlen)
{
	switch(optname)
	{
	case UDT_KEEPALIVE:
		{
			int istrue = *(int*)value;
			if (istrue)
			{
				so_options_ |= UDT_KEEPALIVE;
			}
			else
			{
				so_options_ &= ~UDT_KEEPALIVE;
			}
			break;
		}
	case UDT_SND_BUF:
		{
			size_t v = *(size_t*)value;
			snd_buf_.set_hiwat(v);
			break;
		}
	case UDT_RCV_BUF:
		{
			size_t v = *(size_t*)value;
			rcv_buf_.set_hiwat(v);
			break;
		}
	default:
		{
			BOOST_ASSERT(false);
			return -1;
		}
	}
	return 0;
}

int udt_socket::getsockopt(int optname, void* value, int* optlen)
{
	switch(optname)
	{
	case UDT_KEEPALIVE:
		{
			*(int*)value = (so_options_ & UDT_KEEPALIVE);
			*optlen = 4;
			break;
		}
	case UDT_SND_BUF:
		{
			*(int*)value = snd_buf_.hiwat();
			*optlen = 4;
			break;
		}
	case UDT_RCV_BUF:
		{
			*(int*)value = rcv_buf_.hiwat();
			*optlen = 4;
			break;
		}
	default:
		{
			BOOST_ASSERT(false);
			return -1;
		}
	}
	return 0;
}

size_t udt_socket::consume_pending_data()
{
	LOG_FUNC_SCOPE_C();
	send_ops_t::iterator i = send_ops_.begin();
	size_t consumed = 0;
	while (i != send_ops_.end())
	{
		send_operation& ops = *i;
		handler_t& handler = ops.handler;
		const_buffer_t& cbuf = ops.buf;
		const char* data = (const char*)boost::asio::detail::buffer_cast_helper(cbuf);
		size_t data_len = boost::asio::detail::buffer_size_helper(cbuf);
		if (snd_buf_.append(data, data_len, cb_->m_maxseg))
		{
			LOG_INFO_C("appended !!!");
			get_io_service().post(boost::asio::detail::bind_handler(handler, udt_error::succeed, data_len));
			i = send_ops_.erase(i);
			consumed += data_len;
		}
		else
		{
			LOG_INFO_C("cannot append");
			break;
		}
	}
	pending_data_ -= consumed;
	return consumed;
}

void udt_socket::do_receive()
{
	if (next_layer_ == NULL)
	{
		return;
	}
	const size_t data_len = udt_mtu::MAX_MTU_SIZE_BYTE - udt_hdr::MIN_ALL_HEADERS_SIZE;
	if (rcv_pkt_ == NULL)
	{
		rcv_pkt_ = udt_buf::alloc(data_len);
	}
	next_layer()->async_receive_from(boost::asio::buffer(rcv_pkt_, data_len + sizeof(udt_hdr)),
		remote_endpoint(),
		boost::bind(&udt_socket::handle_receive, this, _1, _2, tracker_.get()));
}

void udt_socket::handle_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	if (wref.expired() || next_layer_ == NULL)
	{
		return;
	}
	if (!e)
	{
		if (remote_endpoint() == peer_endpoint())
		{
			if(cb_->input(rcv_pkt_, bytes_received))	//packet consumed by input, reset to NULL.
			{
				rcv_pkt_ = NULL;
			}
		}
		do_receive();
	}
	else
	{
		LOG_VAR_C(e);
		if (rcv_pkt_ != NULL)
		{
			udt_buf::free(rcv_pkt_);
			rcv_pkt_ = NULL;
		}
		if(e != udt_error::operation_aborted)
		{
			on_disconnected();
		}
	}
}

#ifdef _DEBUG

void test_udt_socket()
{
	boost::asio::io_service ios;
	udt_socket socket(ios);
}

#endif

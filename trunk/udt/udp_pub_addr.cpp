#include "udp_pub_addr.h"
#include <boost/bind.hpp>
#include <boost/date_time.hpp>

unsigned int udp_pub_addr::s_trid_ = 0;

udp_pub_addr::udp_pub_addr(boost::asio::io_service& ios):
ios_(ios),
timer_(ios),
trid_(0)
{
}

udp_pub_addr::~udp_pub_addr(void)
{
	close();
}

int udp_pub_addr::create(udp_socket_t* sk, const std::string& server_hostname, unsigned short server_port)
{
	socket_ = sk;
	server_hostname_ = server_hostname;
	server_port_ = server_port;
	if (tracker_.is_null())
	{
		tracker_.set();
	}
	return 0;
}

void udp_pub_addr::async_pub_addr(const pub_addr_handler_t& handler)
{
	handler_ = handler;
	if (tracker_.is_null())
	{
		tracker_.set();
	}
	pub_addr_ep_ = udp_endpoint_t();
	if (server_ep_ == udp_endpoint_t())
	{
		async_resolve();
	}
	else
	{
		send_request();
	}
}

void udp_pub_addr::cancel()
{
	boost::system::error_code ignored;
	tracker_.reset();
	timer_.cancel(ignored);
	if (socket_ != NULL)
	{
		socket_->cancel(ignored);
	}
	if (resolver_)
	{
		resolver_->cancel();
		resolver_.reset();
	}
}

void udp_pub_addr::close()
{
	//cancel();
}

void udp_pub_addr::send_request()
{
	trid_ = ++s_trid_;
	std::string snd_buf = "A=" + boost::lexical_cast<std::string>(trid_);
	async_receive();
	boost::system::error_code e;
	for (int i = 0; i < DFT_SEND_COUNT; i++)
	{
		socket_->send_to(boost::asio::buffer(snd_buf), server_ep_, 0, e);
		if (e)
		{
			last_error_ = e;
			notify();
			return;
		}
	}
}

void udp_pub_addr::async_resolve()
{
	boost::system::error_code e;
	server_ep_ = udp_endpoint_t(boost::asio::ip::address_v4::from_string(server_hostname_.c_str(), e), server_port_);
	if (!e)
	{
		send_request();
	}
	else
	{
		udp_protocol_t::resolver_query query(server_hostname_, boost::lexical_cast<std::string>(server_port_));
		resolver_.reset( new udp_protocol_t::resolver(ios_));
		resolver_->async_resolve(query, boost::bind(&udp_pub_addr::handle_async_resolve, this, _1, _2, tracker_.get()));
	}
}

void udp_pub_addr::handle_async_resolve(const boost::system::error_code& e, udp_protocol_t::resolver_iterator i, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	if (!e && i != udp_protocol_t::resolver_iterator())
	{
		server_ep_ = *i;
		send_request();
	}
	else
	{
		last_error_ = e;
		notify();
	}
}

void udp_pub_addr::async_receive()
{
	async_wait();
	socket_->async_receive_from(boost::asio::buffer(rcv_buf_, sizeof(rcv_buf_)),
		remote_ep_,
		boost::bind(&udp_pub_addr::handle_async_receive, this, _1, _2, tracker_.get())
		);
}

static bool parse_endpoint(const std::string& str, udp_endpoint_t& ep)
{
	size_t pos = str.find(':');
	if (pos == std::string::npos)
	{
		return false;
	}
	pos++;
	while(str[pos] == ' ')pos++;
	size_t pos1 = str.rfind(':');
	if (pos1 == std::string::npos)
	{
		return false;
	}
	std::string ip = str.substr(pos, pos1- pos);
	unsigned short port = atoi(str.c_str() + pos1 + 1);
	boost::system::error_code ignored;
	ep = udp_endpoint_t(boost::asio::ip::address_v4::from_string(ip, ignored), port);
	return true;
}

void udp_pub_addr::handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	if (!e && bytes_received > 0)
	{
		if (remote_ep_ == server_ep_)
		{
			//A=1|RA: 123.123.254.129:1291
			rcv_buf_[bytes_received] = '\0';
			char* cp = strchr(rcv_buf_, '=');
			if (cp != NULL)
			{
				unsigned int trid = atoi(cp + 1);
				if (trid == trid_)
				{
					cp = strchr(rcv_buf_, '|');
					if(cp != NULL && parse_endpoint(cp + 1, pub_addr_ep_))
					{
						notify();
						return;
					}
				}
			}
		}
		async_receive();
	}
	else
	{
		last_error_ = e;
		notify();
	}
}

void udp_pub_addr::async_wait()
{
	timer_.expires_from_now(boost::posix_time::seconds(DFT_TIMEOUT_S));
	timer_.async_wait(boost::bind(&udp_pub_addr::handle_async_wait, this, _1, tracker_.get()));
}

void udp_pub_addr::handle_async_wait(const boost::system::error_code& e, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	last_error_ = boost::asio::error::timed_out;
	notify();
}

void udp_pub_addr::notify()
{
	ios_.post(boost::asio::detail::bind_handler(handler_, last_error_, pub_addr_ep_));
}

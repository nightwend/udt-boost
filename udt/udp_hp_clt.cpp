
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/function.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assert.hpp>
#include <map>
#include <string>
#include <vector>
#include "tracker.h"
#include <algorithm>
#include "udt_defs.h"
#include "udt_log.h"
#include "utils.h"
#include "udp_defs.h"
#include "udp_hp_clt.h"

udp_hp_clt::udp_hp_clt(boost::asio::io_service& ios):
ios_(ios),
timer_(ios),
socket_(NULL)
{}

udp_hp_clt::~udp_hp_clt(void)
{
	close();
}

int udp_hp_clt::create(udp_socket_t* sk)
{
	LOG_FUNC_SCOPE_C();
	if (sk == NULL || !sk->is_open())	//must opened.
	{
		BOOST_ASSERT(false);
		return -1;
	}
	boost::system::error_code ignored;
	if(sk->local_endpoint(ignored) == udp_endpoint_t())	//must binded.
	{
		BOOST_ASSERT(false);
		return -1;
	}
	socket_ = sk;
	if (tracker_.is_null())
	{
		tracker_.set();
	}
	rcv_buf_.resize(DFT_RCV_BUF_SIZE);
	return 0;
}

void udp_hp_clt::cancel()
{
	LOG_FUNC_SCOPE_C();
	boost::system::error_code ignored;
	tracker_.reset();
	if (socket_)
	{
		socket_->cancel(ignored);
	}
	timer_.cancel(ignored);
}

void udp_hp_clt::close()
{
	LOG_FUNC_SCOPE_C();
	if (socket_ == NULL)
	{
		return;
	}
	cancel();
	socket_ = NULL;
	handler_.clear();
	rcv_buf_.clear();
	last_error_.clear();
}

void udp_hp_clt::async_hole_punch(const hole_punch_handler_t& handler, const udp_endpoints_t& eps)
{
	BOOST_ASSERT(socket_ != NULL);
	handler_ = handler;
	for (udp_endpoints_t::const_iterator i = eps.begin(); i != eps.end(); ++i)
	{
		typedef std::pair<udp_endpoint_t, endpoint_info_t> pair_endpoint_info_t;
		endpoints_info_t::iterator it = peer_eps_.insert(pair_endpoint_info_t(*i, endpoint_info_t())).first;
		endpoint_info_t& info = it->second;
		info.state = endpoint_info_t::TCPS_SYN_SENT;
	}
	output();
	async_receive();
	async_wait(DFT_TIMEOUT_COUNT);
}

std::string udp_hp_clt::prepare_send_peer_data(u_char flags)
{
	std::string buf(sizeof(udt_hdr), '\0');
	udt_hdr* hdr = (udt_hdr*)&(buf[0]);
	hdr->uh_flags = flags | udt_hdr::TH_HP;
	hdr->uh_seq = 0;
	hdr->uh_ack = 0;
	hdr->uh_len = 0;
	hdr->uh_off = sizeof(udt_hdr);
	hdr->uh_ver = 0;
	hdr->uh_win = 0;
	hdr->uh_ver = 0;
	hdr->hton();
	return buf;
}

void udp_hp_clt::output()
{
	BOOST_ASSERT(socket_ != NULL);
	boost::system::error_code e;
	for (endpoints_info_t::iterator i = peer_eps_.begin(); i != peer_eps_.end(); ++i)
	{
		endpoint_info_t& info = i->second;
		switch (info.state)
		{
		case endpoint_info_t::TCPS_SYN_SENT:
			info.snd_buf = prepare_send_peer_data(udt_hdr::TH_SYN);
			break;
		case endpoint_info_t::TCPS_SYN_RECEIVED:
			info.snd_buf = prepare_send_peer_data(udt_hdr::TH_SYN | udt_hdr::TH_ACK);
			break;
		case endpoint_info_t::TCPS_ESTABLISHED:
			info.snd_buf = prepare_send_peer_data(udt_hdr::TH_ACK);
			break;
		}
		if (!info.snd_buf.empty())
		{
			for (int count = DFT_SEND_PACKETS; count > 0; count--)
			{
				socket_->send_to(boost::asio::buffer(info.snd_buf), 
					i->first,
					0, 
					e);
			}
		}
	}
}

void udp_hp_clt::async_receive()
{
	socket_->async_receive_from(boost::asio::buffer(&rcv_buf_[0], rcv_buf_.length()),
		remote_ep_,
		boost::bind(&udp_hp_clt::handle_async_receive, this, _1, _2, tracker_.get()));
}

void udp_hp_clt::handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	if (e == boost::asio::error::operation_aborted || wref.expired())
	{
		return;
	}
	endpoints_info_t::iterator i = peer_eps_.find(remote_ep_);
	LOG_VAR_C(remote_ep_);
	if (!e)
	{
		if (i != peer_eps_.end())
		{
			handle_peer_resp(&rcv_buf_[0], bytes_received, i);
			endpoint_info_t& info = i->second;
			if (info.state == endpoint_info_t::TCPS_ESTABLISHED)
			{
				notify();
				return;
			}
		}
		async_receive();
	}
	else
	{
		if (i != peer_eps_.end())
		{
			peer_eps_.erase(i);
			if (peer_eps_.empty())
			{
				last_error_ = e;
				notify();
				return;
			}
		}
		async_receive();
	}
}

void udp_hp_clt::handle_peer_resp(char* ptr, int len, endpoints_info_t::iterator it)
{
	LOG_FUNC_SCOPE_C();
	udt_hdr* hdr = (udt_hdr*)ptr;
	LOG_VAR_C(has_hp(hdr->uh_flags));
	LOG_VAR_C(has_syn(hdr->uh_flags));
	LOG_VAR_C(has_ack(hdr->uh_flags));
	if (len != sizeof(udt_hdr) || 
		!has_hp(hdr->uh_flags))
	{
		return;
	}
	hdr->ntoh();
	endpoint_info_t& info = it->second;
	if (info.state == endpoint_info_t::TCPS_ESTABLISHED)
	{
		return;
	}
	bool need_output = false;
	LOG_VAR_C(info.state);
	if (has_syn(hdr->uh_flags))
	{
		switch (info.state)
		{
		case endpoint_info_t::TCPS_CLOSED:
		case endpoint_info_t::TCPS_SYN_SENT:	//
			{
				need_output = true;
				info.state = endpoint_info_t::TCPS_SYN_RECEIVED;	//passive open.
				break;
			}
		case endpoint_info_t::TCPS_SYN_RECEIVED:	//duplicate syn, do not ack.
			{
				break;
			}
		default:
			{
				break;
			}
		}
	}
	LOG_VAR_C(info.state);
	if (has_ack(hdr->uh_flags))
	{
		switch (info.state)
		{
		case endpoint_info_t::TCPS_SYN_RECEIVED:
			{
				info.state = endpoint_info_t::TCPS_ESTABLISHED;
				break;
			}
		default:
			{
				break;
			}
		}
	}
	LOG_VAR_C(info.state);
	LOG_VAR_C(need_output);
	if (need_output)
	{
		output();
	}
}

void udp_hp_clt::notify()
{
	udp_endpoints_t eps;
	for (endpoints_info_t::iterator i = peer_eps_.begin(); i != peer_eps_.end(); ++i)
	{
		if (i->second.state == endpoint_info_t::TCPS_ESTABLISHED)
		{
			LOG_VAR_C(i->first);
			eps.push_back(i->first);
		}
	}
	ios_.post(boost::asio::detail::bind_handler(handler_, last_error_, eps));
}

void udp_hp_clt::async_wait(int remain_count)
{
	if (remain_count <= 0)
	{
		last_error_ = boost::asio::error::timed_out;
		notify();
		return;
	}
	timer_.expires_from_now(boost::posix_time::seconds(DFT_TIMEOUT_S));
	timer_.async_wait(boost::bind(&udp_hp_clt::handle_async_timeout, this, _1, remain_count, tracker_.get()));
}

void udp_hp_clt::handle_async_timeout(const boost::system::error_code& e, int remain_count, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	if (e == boost::asio::error::operation_aborted || wref.expired())
	{
		return;
	}
	output();
	async_wait(--remain_count);
}

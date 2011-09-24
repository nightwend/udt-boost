#include "udt_mtu.h"
#include "udt_defs.h"
#include <boost/bind.hpp>
#include <boost/crc.hpp>
#include "udt_log.h"
#include "udt_error.h"

const static u_long PACKET_MAXIMUMS[] = {
	1500,   // Ethernet, Point-to-Point (default)
	1492,   // IEEE 802.3
	1006,   // SLIP, ARPANET
	576,    // X.25 Networks
	544,    // DEC IP Portal
	512,    // NETBIOS
	508,    // IEEE 802/Source-Rt Bridge, ARCNET
	296,    // Point-to-Point (low delay)
	68,     // Official minimum
	0,      // End of list marker
};

udt_mtu::udt_mtu(boost::asio::io_service& ios):
ios_(ios),
timer_(ios),
socket_(ios),
current_mtu_idx_(0),
mtu_hint_(0),
mtu_(0)
{
	
}

udt_mtu::~udt_mtu()
{
	close();
}

void udt_mtu::init()
{
	mtu_hint_ = 0;
	mtu_ = 0;
	current_mtu_idx_ = 0;
	last_error_.clear();
	rcv_buffer_.resize(2000);
	if (tracker_.is_null())
	{
		tracker_.set();
	}
}

void udt_mtu::open_socket()
{
	if (!socket_.is_open())
	{
		boost::system::error_code ignored;
		socket_.open(protocol_t::v4(), ignored);
		socket_.bind(endpoint_t(boost::asio::ip::address_v4::any(), 0), ignored);
		DWORD val = TRUE;
		setsockopt(socket_.native(), IPPROTO_IP, IP_DONTFRAGMENT, (const char*)&val, sizeof(val));
	}
}

int udt_mtu::create()
{
	init();
	open_socket();
	return 0;
}

void udt_mtu::async_probe_mtu(const endpoint_t& rep, const handler_mtu_t& handler)
{
	remote_endpoint_ = rep;
	handler_ = handler;
	send_echo();
}

void udt_mtu::close()
{
	LOG_FUNC_SCOPE_C();
	cancel();
	snd_buffer_.clear();
	rcv_buffer_.clear();
}

void udt_mtu::cancel()
{
	LOG_FUNC_SCOPE_C();
	tracker_.reset();
	timer_.cancel(last_error_);
	socket_.cancel(last_error_);
	last_error_ = udt_error::operation_aborted;
}

void udt_mtu::send_echo()
{
	boost::system::error_code e;
	while (prepare_data())
	{
		for (int i = DEFAULT_SEND_COUNT - 1; i >= 0; i--)
		{
			socket_.send_to(boost::asio::buffer(snd_buffer_),
				remote_endpoint_,
				0,
				e);
		}
		if (!e)
		{
			async_recv_reply();
			return;
		}
		else if (e.value() == ERROR_INVALID_USER_BUFFER)
		{
			continue;
		}
		else
		{
			last_error_ = e;
			break;
		}
	}
	notify();
}

void udt_mtu::async_recv_reply()
{
	LOG_FUNC_SCOPE_C();
	async_wait();
	socket_.async_receive_from(boost::asio::buffer((void *)&rcv_buffer_[0], rcv_buffer_.length()),
		reply_endpoint_, 
		boost::bind(&udt_mtu::handle_recv, this, _1, _2, tracker_.get()));
}

void udt_mtu::async_wait()
{
	timer_.expires_from_now(boost::posix_time::milliseconds(DEFAULT_TIMEOUT_MS));
	timer_.async_wait(boost::bind(&udt_mtu::handle_timeout, this, _1, tracker_.get()));
}

void udt_mtu::handle_timeout(const boost::system::error_code& e, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	if (wref.expired())
	{
		return;
	}
	if (e != boost::asio::error::operation_aborted)
	{
		last_error_ = udt_error::timed_out;
		notify();
	}
}

inline void udt_mtu::notify()
{
	cancel();
	get_io_service().post(boost::asio::detail::bind_handler(handler_, last_error_, mtu_));
}

inline icmp* get_icmp(std::string& buf)
{
	return (icmp*)&(buf[0]);
}

inline icmp* get_icmp_skip_ip(std::string& buf)
{
	return (icmp*)&(buf[udt_mtu::IPHDR_SIZE_BYTE]);
}

void udt_mtu::handle_recv(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref)
{
	LOG_VAR_C(e);
	if (wref.expired())
	{
		return;
	}
	boost::system::error_code ignored;
	timer_.cancel(ignored);

	if (e == boost::asio::error::operation_aborted)
	{
		return;
	}
	
	if (!e && bytes_received >= sizeof(icmp) + IPHDR_SIZE_BYTE)
	{
		icmp* ip = get_icmp_skip_ip(rcv_buffer_);
		int icmp_type = ip->icmp_type;
		int icmp_code = ip->icmp_code;
		LOG_VAR_C(icmp_type);
		LOG_VAR_C(icmp_code);
		if (icmp_type == ICMP_ECHOREPLY && icmp_code == 0)
		{
			notify();
			return;
		}
		else if (icmp_type == ICMP_UNREACH)
		{
			switch (icmp_code)
			{
			case ICMP_UNREACH_NET:
				last_error_ = boost::asio::error::network_unreachable;
				break;
			case ICMP_UNREACH_HOST:
				last_error_ = boost::asio::error::host_unreachable;
				break;
			case ICMP_UNREACH_PROTOCOL:
				last_error_ = boost::asio::error::not_found;
				break;
			case ICMP_UNREACH_PORT:
				last_error_ = boost::asio::error::host_unreachable;
				break;
			case ICMP_UNREACH_NEEDFRAG:
				{
					mtu_hint_ = ntohs(ip->icmp_nextmtu);
					send_echo();	//try again.
					return;
				}
			case ICMP_UNREACH_HOST_UNKNOWN:
				break;
			default:
				last_error_ = boost::asio::error::timed_out;	
				break;
			}
		}
	}
	else if (!e && bytes_received < sizeof(icmp))
	{
		last_error_ = boost::asio::error::no_buffer_space;
	}
	notify();
}

bool udt_mtu::prepare_data()
{
	if (mtu_hint_ != 0)
	{
		mtu_ = mtu_hint_;
		for (int i = 0; i < _countof(PACKET_MAXIMUMS); i++)
		{
			if (PACKET_MAXIMUMS[i] <= mtu_hint_)
			{
				current_mtu_idx_ = i;
				break;
			}
		}
	}
	else
	{
		if (current_mtu_idx_ >= _countof(PACKET_MAXIMUMS))
		{
			return false;
		}
		mtu_ = PACKET_MAXIMUMS[current_mtu_idx_];
	}
	LOG_VAR_C(mtu_);
	snd_buffer_.resize(mtu_ - IPHDR_SIZE_BYTE);
	icmp* ip = get_icmp(snd_buffer_);
	ip->icmp_type = ICMP_ECHO;
	ip->icmp_code = 0;
	ip->icmp_id = htons(1234);
	ip->icmp_seq = htons(1234);
	current_mtu_idx_++;
	return true;
}

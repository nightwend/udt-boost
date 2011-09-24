#include "udp_chknat.h"
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>
#include <boost/date_time.hpp>
#include <algorithm>
#include <stdlib.h>
#include <vector>
#include "utils.h"

udp_endpoint_t udp_chknat::server_eps_[2];

udp_chknat::udp_chknat(boost::asio::io_service& ios):
ios_(ios),
resolver_(ios),
timer_(ios),
socket_(NULL)
{
}

udp_chknat::~udp_chknat(void)
{
}

int udp_chknat::create(udp_socket_t* sk)
{
	socket_ = sk;
	if (tracker_.is_null())
	{
		tracker_.set();
	}
	memset(resp_bitmap_, false, sizeof(resp_bitmap_));
	return 0;
}

void udp_chknat::async_chknat(const chknat_handler_t& handler)
{
	handler_ = handler;
	bool all_resolved = true;
	if (server_eps_[0] == udp_endpoint_t())
	{
		all_resolved = false;
		async_resolve_server(0);
	}
	if (server_eps_[1] == udp_endpoint_t())
	{
		all_resolved = false;
		async_resolve_server(1);
	}
	if (all_resolved)
	{
		send_request();
	}
}

void udp_chknat::cancel()
{
	boost::system::error_code ignored;
	tracker_.reset();
	timer_.cancel(ignored);
	resolver_.cancel();
	if (socket_ != NULL)
	{
		socket_->cancel(ignored);
	}
}

void udp_chknat::close()
{
	cancel();
}

void udp_chknat::async_resolve_server(int server_idx)
{
	const static std::string server_ips[2] = 
	{
		"n1.msnlite.org",
		"n2.msnlite.org"
	};
	const static std::string server_ports[2] =
	{
		"25000",
		"25000"
	};

	assert(server_idx >= 0 && server_idx < _countof(server_eps_));
	udp_protocol_t::resolver_query query(server_ips[server_idx], server_ports[server_idx]);
	resolver_.async_resolve(query, 
		boost::bind(&udp_chknat::handle_async_resolve_server, this, _1, _2, server_idx, tracker_.get()));
}

void udp_chknat::handle_async_resolve_server(const boost::system::error_code& e, udp_protocol_t::resolver_iterator i, int server_idx, const tracker::weak_refer& wref)
{
	if (wref.expired())
	{
		return;
	}
	if (e || i == udp_protocol_t::resolver_iterator())
	{
		last_error_ = e;
		notify();
		return;
	}
	server_eps_[server_idx] = *i;
	if (server_eps_[0] != udp_endpoint_t() && server_eps_[1] != udp_endpoint_t())
	{
		send_request();
	}
}

void udp_chknat::send_request()
{
	const static std::string buf[4] = 
	{
		"A=0",	//向服务器的(IP-1,Port-1)发送数据包要求服务器返回客户端的IP和Port
		"A=1",	//向服务器的(IP-2,Port-2)发送数据包要求服务器返回客户端的IP和Port
		"B=2",	//向服务器的(IP-2,Port-2)发送数据包要求服务器从另一个IP返回客户端的IP和Port
		"C=3"	//向服务器的(IP-2,Port-2)发送数据包要求服务器从另一个端口返回客户端的IP和Port
	};
	async_receive();
	boost::system::error_code ignored;
	for (int i = 0; i < _countof(buf); i++)
	{
		int server_idx = (i != 0);
		for (int j = 0; j < DFT_SEND_COUNT; j++)
		{
			socket_->send_to(boost::asio::buffer(buf[i]), server_eps_[server_idx], 0, ignored);
		}
	}
}

void udp_chknat::async_receive()
{
	async_wait();
	socket_->async_receive_from(boost::asio::buffer(rcv_buf_, sizeof(rcv_buf_)),
		remote_ep_,
		boost::bind(&udp_chknat::handle_async_receive, this, _1, _2, tracker_.get()));
}

void udp_chknat::handle_async_receive(const boost::system::error_code& e, size_t bytes_received, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	boost::system::error_code ignored;
	timer_.cancel(ignored);
	if (!e && bytes_received > 0)
	{
		if (std::find(server_eps_, server_eps_ + _countof(server_eps_), remote_ep_) != server_eps_ + _countof(server_eps_))	//really from one of our server.
		{
			//A=1 | 127.0.0.1: 80
			rcv_buf_[bytes_received] = '\0';
			char* cp = strchr(rcv_buf_, '=');
			if (cp != NULL)
			{
				cp++;
				int idx = atoi(cp);
				if (idx >=0 || idx < _countof(nat_info_.eps))
				{
					cp = strchr(rcv_buf_, '|');
					if(cp != NULL && parse_endpoint(cp + 1, nat_info_.eps[idx]))
					{
						resp_bitmap_[idx] = true;
						if (finish())
						{
							analyze();
							return;
						}
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

void udp_chknat::async_wait()
{
	timer_.expires_from_now(boost::posix_time::seconds(DFT_TIMEOUT_S));
	timer_.async_wait(boost::bind(&udp_chknat::handle_async_wait, this, _1, tracker_.get()));
}

void udp_chknat::handle_async_wait(const boost::system::error_code& e, const tracker::weak_refer& wref)
{
	if (wref.expired() || e == boost::asio::error::operation_aborted)
	{
		return;
	}
	analyze();
}

void udp_chknat::analyze()
{
	if (nat_info_.eps[0] == udp_endpoint_t())
	{
		nat_info_.nat_type = nt_fw_block;
		notify();
		return;
	}

	typedef std::vector<udp_endpoint_t> endpoints_t;
	boost::system::error_code ignored;
	endpoints_t local_eps;
	get_local_endpoints_v4<udp_protocol_t, endpoints_t>(ios_, 
		boost::lexical_cast<std::string>(socket_->local_endpoint(ignored).port()).c_str(),
		local_eps);
	if (std::find(local_eps.begin(), local_eps.end(), nat_info_.eps[0]) != local_eps.end())
	{
		nat_info_.nat_type = nt_wan;
		notify();
		return;
	}
	
	if (nat_info_.eps[0].port() != nat_info_.eps[1].port())
	{
		nat_info_.nat_type = nt_symmetric;
		notify();
		return;
	}
	
	if (nat_info_.eps[2] != udp_endpoint_t() && nat_info_.eps[3] != udp_endpoint_t())
	{
		nat_info_.nat_type = nt_full_cone;
		notify();
		return;
	}

	if (nat_info_.eps[2] == udp_endpoint_t() && nat_info_.eps[3] == udp_endpoint_t())
	{
		nat_info_.nat_type = nt_port_restrict_cone;
		notify();
		return;
	}

	nat_info_.nat_type = nt_restrict_cone;
	notify();
}

bool udp_chknat::parse_endpoint(const std::string& str, udp_endpoint_t& ep)
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

void udp_chknat::notify()
{
	cancel();
	ios_.post(boost::asio::detail::bind_handler(handler_, last_error_, nat_info_));
}

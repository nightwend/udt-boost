#pragma once
#include <boost/asio/ip/udp.hpp>
#include <vector>

typedef boost::asio::ip::udp udp_protocol_t;

typedef udp_protocol_t::endpoint udp_endpoint_t;

typedef udp_protocol_t::socket udp_socket_t;

typedef std::vector<udp_endpoint_t> udp_endpoints_t;

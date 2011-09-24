#include <iostream>
#include "udt/udt_socket.h"
#include <boost/asio.hpp>
#include "udt/udt_log.h"
#include "udt/ticker.h"
#include "udt_test_client.h"
#include "udt_test_server.h"
#include "udt/udt_mtu.h"
#include "udt/udt_option.h"
#include "udt/iudt_engine.h"

using namespace boost::asio;

io_service ios;
udt_socket us(ios);
std::string s = "0123456789";
int counter = 0;
int total_send = 0;

LOG_IMPL_GLOBAL();

void udt_handle_send(const boost::system::error_code& e, size_t bytes_sent)
{
	using namespace std;
	if (++counter < 1000)
	{
		us.async_send(boost::asio::buffer(s), udt_handle_send);
	}
	else
	{
		boost::system::error_code e;
		us.close(e);
	}
	total_send += bytes_sent;
	std::cout<<total_send<<std::endl;
	LOG_VAR(total_send);
	LOG_VAR(counter);
	//us.async_send(boost::asio::buffer(s), handle_send);
}

void udt_handle_connect(const boost::system::error_code& e )
{
	LOG_VAR(e);
	us.async_send(boost::asio::buffer(s), udt_handle_send);
}

void test()
{
	LOG_INFO("hello,world");
	udt_socket::next_layer_t m_socket(ios);
	m_socket.open(udp_protocol_t::v4());
	m_socket.bind(udp_endpoint_t(ip::address_v4::any(), 1291));
	//us.remote_endpoint() = ip::udp::endpoint(ip::address_v4::from_string("172.16.1.100"), 1290);
	boost::system::error_code e;
	us.create(e, &m_socket);
	s += s;
	s += s;
	s += s;
	s += s;
	s += s;
	s += s;
	s += s;
	s += s;
	//us.async_send(boost::asio::buffer(s), handle_send);
	us.async_connect(udp_endpoint_t(ip::address_v4::from_string(SERVER_IP), 1290), udt_handle_connect);
	ios.run();
}

void main_test_recv_file()
{
	udt_test_recvfile("e:\\a.db", CLIENT_IP, CLIENT_PORT);
}

void main_test_send_file()
{
	udt_test_sendfile("e:\\sqlite_client.rar", SERVER_IP, SERVER_PORT);
}

void handle_async_mtu(const boost::system::error_code& e, size_t mtu)
{

}

void test_option_sack()
{
	udt_option_sack ops;
	udt_buf::list_packets_t reass;
	udt_packet* p = udt_buf::alloc(100);
	p->hdr.uh_seq = 0;
	reass.push_back(p);
	
	p = udt_buf::alloc(100);
	p->hdr.uh_seq = 100;
	reass.push_back(p);

	p = udt_buf::alloc(100);
	p->hdr.uh_seq = 200;
	reass.push_back(p);

	p = udt_buf::alloc(100);
	p->hdr.uh_seq = 300;
	reass.push_back(p);

	p = udt_buf::alloc(100);
	p->hdr.uh_seq = 400;
	reass.push_back(p);

	ops.compose(reass.rbegin(), reass.rend(), 100);
}

int main()
{
	LOG_INIT();
	iudt_engine::get_instance()->run();
	//test();
	//test_pmtu_client();
	
	test_udt_engine();
	//test_udt_engine_server();
	while(1)
		Sleep(10000);
	
	//test_hole_punch_client();
	//test_udp_chknat();
	//test_udp_pub_addr();
	//test_hole_punch_server();
	//main_test_recv_file();
	//tcp_test_sendfile("e:\\sqlite_client.rar");
	//tcp_test_recvfile("c:\\a.db");
	//udt_mtu mtu(ios);
	//mtu.async_probe_mtu(udt_mtu::endpoint_t(ip::address_v4::from_string("61.135.169.125"), 80), handle_async_mtu);
	
	//test_option_sack();

	//test_active_connect();
	//test_passive_connect();

	//test_duplex_file_client();
	//test_duplex_file_server();
	//test_keepalive_client();
	//test_keepalive_server();
	//test_persist_client();
	//test_persist_server();
	//boost::system::error_code e;
	//ios.run(e);
	//LOG(e);
	iudt_engine::get_instance()->stop();
	return 0;
}

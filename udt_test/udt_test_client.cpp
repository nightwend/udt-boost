#include "udt_test_client.h"
#include "udt/udt_socket.h"
#include <stdio.h>
#include <boost/asio.hpp>
#include <iostream>
#include <boost/assert.hpp>
#include "udt/udt_log.h"
#include "udt/ticker.h"
#include "udt/udt_cb.h"
#include "udt/udt_error.h"
#include "udt_test_server.h"
#include "udt/udp_hp_clt.h"
#include "udt/udt_mtu.h"
#include "udt/udp_chknat.h"
#include "udt/udp_pub_addr.h"
#include "udt/iudt_engine.h"

using namespace boost::asio;

static io_service ios;
static udt_socket udt_sk(ios);
static udt_socket::next_layer_t raw_sk(ios);
static ip::tcp::socket tcp_sk(ios);
static char rcv_buf[8192];
static char snd_buf[8192];
static FILE* fpSrc = NULL;
static FILE* fpDest = NULL;
static ticker<milli_second> milli_tk;

static void init_udt_socket()
{
	if (!raw_sk.is_open())
	{
		raw_sk.open(udp_protocol_t::v4());
		raw_sk.bind(udp_endpoint_t(ip::address_v4::any(), CLIENT_PORT));
		boost::system::error_code e;
		udt_sk.create(e, &raw_sk);
	}
}

static void handle_shutdown(const boost::system::error_code& e)
{
	LOG_VAR(e);
	boost::system::error_code ignored;
	udt_sk.close(ignored);
}

//begin udt_test_sendfile
static void udt_handle_send(const boost::system::error_code& e, size_t bytes_send)
{
	LOG_VAR(e);
	if (e == udt_error::disconnected)
	{
		boost::system::error_code e;
		udt_sk.close(e);
		milli_tk.toc();
		LOG_VAR(milli_tk.duration());
		return;
	}
	size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
	if (read != 0)
	{
		udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, read), 
			udt_handle_send);
	}
	LOG_VAR(read);
	if (read < sizeof(snd_buf))
	{
		udt_sk.async_shutdown(handle_shutdown);
	}
}

static void udt_handle_connect(const boost::system::error_code& e)
{
	LOG_VAR(e);
	if (!e)
	{
		size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
		if (read != 0)
		{
			udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, read), 
				udt_handle_send);
		}
		LOG_VAR(read);
		if (read < sizeof(snd_buf))
		{
			udt_sk.async_shutdown(handle_shutdown);
		}
	}
}

void udt_test_sendfile(const char* filename, const char* ip, unsigned short port)
{
	init_udt_socket();
	udt_sk.async_connect(udp_endpoint_t(ip::address_v4::from_string(ip), port), udt_handle_connect);
	fpSrc = fopen(filename, "rb");
	milli_tk.tic();
	//ios.run();
}
//end udt_test_sendfile

//begin tcp_test_sendfile
static void tcp_handle_send(const boost::system::error_code& e, size_t bytes_send);

static void tcp_async_send()
{
	size_t read = fread(snd_buf, 1, _countof(snd_buf), fpSrc);
	if (read > 0)
	{
		tcp_sk.async_send(buffer(snd_buf, read), tcp_handle_send);
	}
	else
	{
		tcp_sk.close();
		fclose(fpSrc);
		fpSrc = NULL;
		milli_tk.toc();
		std::cout<<"milli_tk.duration() = "<<milli_tk.duration()<<std::endl;
		LOG_VAR(milli_tk.duration());
	}
}

static void tcp_handle_send(const boost::system::error_code& e, size_t bytes_send)
{
	LOG_VAR(e);
	if (!e)
	{
		tcp_async_send();
	}
}

static void tcp_handle_connect(const boost::system::error_code& e)
{
	if (!e)
	{
		tcp_async_send();
	}
}

void tcp_test_sendfile(const char* filename)
{
	tcp_sk.open(ip::tcp::v4());
	tcp_sk.bind(ip::tcp::endpoint(ip::address_v4::any(), CLIENT_PORT));
	tcp_sk.async_connect(ip::tcp::endpoint(ip::address_v4::from_string(SERVER_IP), SERVER_PORT),
		tcp_handle_connect);
	fpSrc = fopen(filename, "rb");
	milli_tk.tic();
	ios.run();
}
//end tcp_test_sendfile

//begin test_active_connect
void test_active_connect()
{
	udt_socket::next_layer_t raw_sk(ios);
	raw_sk.open(udp_protocol_t::v4());
	raw_sk.bind(udp_endpoint_t(ip::address_v4::any(), CLIENT_PORT));
	udt_sk.peer_endpoint() = udp_endpoint_t(ip::address_v4::from_string(SERVER_IP), SERVER_PORT);
	boost::system::error_code e;
	udt_sk.create(e, &raw_sk);
	udt_sk.cb_->respond(0, 0, udt_hdr::TH_SYN);
	Sleep(100);
	udt_sk.cb_->respond(0, 0, udt_hdr::TH_SYN);
	Sleep(100);
	udt_sk.cb_->respond(0, 0, udt_hdr::TH_SYN);	//check trible syn.
	Sleep(100);

	udt_sk.cb_->respond(1, 0, udt_hdr::TH_SYN | udt_hdr::TH_ACK);
	Sleep(100);
	udt_sk.cb_->respond(1, 0, udt_hdr::TH_SYN | udt_hdr::TH_ACK);	//check double syn | ack.
	Sleep(100);

	udt_sk.cb_->respond(0, 0, udt_hdr::TH_SYN);	//check trible syn.
	Sleep(100);

	udt_sk.cb_->respond(1, 0, udt_hdr::TH_FIN | udt_hdr::TH_ACK);
	Sleep(100);

	udt_sk.cb_->respond(1, 0, udt_hdr::TH_FIN | udt_hdr::TH_ACK);
	Sleep(100);

	udt_sk.cb_->respond(2, 0, udt_hdr::TH_FIN | udt_hdr::TH_ACK);
	Sleep(100);

	udt_sk.cb_->respond(1, 1, udt_hdr::TH_FIN | udt_hdr::TH_ACK);
	Sleep(100);

	udt_sk.cb_->respond(2, 2, udt_hdr::TH_FIN | udt_hdr::TH_ACK);
	Sleep(100);
}
//end test_active_connect

//begin test_duplex_file_client
static void handle_duplex_send(const boost::system::error_code& e, size_t byte_sent)
{
	LOG_VAR(e);
	static size_t total_sent = 0;
	total_sent += byte_sent;
	LOG_VAR(total_sent);
	LOG_VAR(byte_sent);
	if (e == udt_error::disconnected)
	{
		boost::system::error_code e;
		udt_sk.close(e);	//connection closed.
		return;
	}
	size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
	if (read != 0)
	{
		udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, read), 
			handle_duplex_send);
	}
	LOG_VAR(read);
	if (read < sizeof(snd_buf))
	{
		udt_sk.async_shutdown(handle_shutdown);
	}
}

static void handle_duplex_receive(const boost::system::error_code& e, size_t byte_received)
{
	LOG_VAR(e);
	LOG_VAR(byte_received);
	static int total_received = 0;
	total_received += total_received;
	LOG_VAR(total_received);
	if (e == udt_error::disconnected)
	{
		boost::system::error_code e;
		udt_sk.close(e);	//connection closed.
		return;
	}
	if (byte_received)
	{
		fwrite(rcv_buf, sizeof(rcv_buf[0]), byte_received, fpDest);
	}
	if (e == udt_error::eof)
	{
		LOG_INFO("finished receive");
		fclose(fpDest);
		fpDest = NULL;
	}
	else if(!e)
	{
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)),
			handle_duplex_receive);
	}
	else
	{
		BOOST_ASSERT(false);
	}
}

static void handle_duplex_connect(const boost::system::error_code& e)
{
	static int count = 0;
	LOG_VAR(e);
	if (!e)
	{
		size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
		if (read != 0)
		{
			udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, read), 
				handle_duplex_send);
		}
		if (read < sizeof(snd_buf))
		{
			udt_sk.async_shutdown(handle_shutdown);
		}
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)),
			handle_duplex_receive);
	}
}

void test_duplex_file_client()
{
	init_udt_socket();
	udt_sk.async_connect(udp_endpoint_t(ip::address_v4::from_string(SERVER_IP), SERVER_PORT), 
		handle_duplex_connect);
	fpSrc = fopen("e:\\msgdb.dat", "rb");
	fpDest = fopen("e:\\a.dat", "wb");
	ios.run();
}
//end test_duplex_file_client

//begin test_keepalive_client
static void handle_keepalive_connect(const boost::system::error_code& e)
{
	LOG_VAR(e);
	if (!e)
	{
		;	//do nothing.
	}
}

void test_keepalive_client()
{
	init_udt_socket();
	udt_sk.async_connect(udp_endpoint_t(ip::address_v4::from_string(SERVER_IP), SERVER_PORT), 
		handle_keepalive_connect);
	int istrue = 1;
	udt_sk.setsockopt(udt_socket::UDT_KEEPALIVE, &istrue, sizeof(istrue));
	ios.run();
}
//end test_keepalive_client

//begin test_persist_client
static void handle_persist_send(const boost::system::error_code& e, size_t byte_sent)
{
	if (!e)
	{
		udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, sizeof(snd_buf)), handle_persist_send);
	}
}

static void handle_persist_connect(const boost::system::error_code& e)
{
	LOG_VAR(e);
	if (!e)
	{
		udt_sk.async_send(udt_socket::const_buffer_t(snd_buf, sizeof(snd_buf)), handle_persist_send);
	}
}

void test_persist_client()
{
	init_udt_socket();
	udt_sk.async_connect(udp_endpoint_t(ip::address_v4::from_string(SERVER_IP), SERVER_PORT), 
		handle_persist_connect);
	ios.run();
}
//end test_persist_client

//begin test_hole_punch_client

static void handle_hole_punch(const boost::system::error_code& e, const udp_endpoints_t& eps)
{
	LOG_VAR(e);
	for (udp_endpoints_t::const_iterator i = eps.begin(); i != eps.end(); ++i)
	{
		LOG_VAR(*i);
		udt_test_sendfile("e:\\thread.py", i->address().to_v4().to_string().c_str(), i->port());
	}
	boost::system::error_code ignored;
	udt_sk.close(ignored);
}

void test_hole_punch_client()
{
	udp_hp_clt_default clt(ios);
	init_udt_socket();
	clt.create(&raw_sk);
	clt.async_hole_punch(handle_hole_punch);
	//clt.cancel();
	boost::system::error_code ignored;
	//udt_sk.close(ignored);
	ios.run();
}
//end test_hole_punch_client

//begin test_pmtu_client
static void handle_mtu_probe(const boost::system::error_code& e, size_t mtu)
{
	LOG_VAR(e);
	LOG_VAR(mtu);
}

void test_pmtu_client()
{
	udt_mtu mtu_prober(ios);
	mtu_prober.create();
	mtu_prober.async_probe_mtu(udt_mtu::endpoint_t(boost::asio::ip::address_v4::from_string(SERVER_IP), SERVER_PORT), handle_mtu_probe);
	ios.run();
}
//end test_pmtu_client

//begin test_udp_chknat
static void handle_chknat(const boost::system::error_code& e, const udp_chknat::nat_info& info)
{
	LOG_VAR(e);
}

void test_udp_chknat()
{
	udp_chknat chknat(ios);
	init_udt_socket();
	chknat.async_chknat(handle_chknat);
	chknat.create(&raw_sk);
	ios.run();
}
//end test_udp_chknat

//begin test_udp_pub_addr
udp_pub_addr pub_addr(ios);
static void handle_udp_pub_addr(const boost::system::error_code& e, const udp_endpoint_t& ep)
{
	LOG_VAR(e);
	LOG_VAR(ep);
	static int i = 0;
	if (++i < 10)
	{
		pub_addr.async_pub_addr(handle_udp_pub_addr);
	}
	else
	{
		pub_addr.close();
	}
}

void test_udp_pub_addr()
{
	init_udt_socket();
	//pub_addr.create(&raw_sk);
	pub_addr.async_pub_addr(handle_udp_pub_addr);
	ios.run();
}
//end test_udp_pub_addr

//begin test_udt_engine
static iudt_engine* engine = iudt_engine::get_instance();
static udt_engine_socket* sock = NULL;

static void handle_udt_engine_shutdown(const boost::system::error_code& e)
{
	LOG_VAR(e);
	boost::system::error_code ignored;
	engine->close(sock);
	sock = NULL;
}

static void udt_handle_udt_engine_send(const boost::system::error_code& e, size_t bytes_send)
{
	LOG_VAR(e);
	if (e == udt_error::disconnected)
	{
		boost::system::error_code e;
		engine->close(sock);
		sock = NULL;
		milli_tk.toc();
		LOG_VAR(milli_tk.duration());
		return;
	}
	size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
	if (read != 0)
	{
		engine->async_send(sock, 
			iudt_engine::const_buffer_t(snd_buf, read), 
			udt_handle_udt_engine_send);
	}
	LOG_VAR(read);
	if (read < sizeof(snd_buf))
	{
		engine->async_shutdown(sock, handle_udt_engine_shutdown);
	}
}

static void handle_udt_engine_connect(const boost::system::error_code& e)
{
	LOG_VAR(e);
	if (!e)
	{
		fpSrc = fopen("e:\\sqlite_client.rar", "rb");
		size_t read = fread(snd_buf, sizeof(snd_buf[0]), _countof(snd_buf), fpSrc);
		if (read != 0)
		{
			engine->async_send(sock,
				iudt_engine::const_buffer_t(snd_buf, read), 
				udt_handle_udt_engine_send);
		}
		LOG_VAR(read);
		if (read < sizeof(snd_buf))
		{
			engine->async_shutdown(sock, handle_udt_engine_shutdown);
		}
	}
}

void test_udt_engine()
{
	sock = engine->create(CLIENT_PORT);
	engine->async_connect(sock,
		udp_endpoint_t(boost::asio::ip::address_v4::from_string(SERVER_IP), SERVER_PORT),
		handle_udt_engine_connect);
}
//end test_udt_engine
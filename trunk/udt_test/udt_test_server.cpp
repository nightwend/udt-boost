#include "udt_test_server.h"
#include "udt_test_client.h"
#include "udt/udt_socket.h"
#include <stdio.h>
#include <boost/asio.hpp>
#include <boost/assert.hpp>
#include "udt/udt_log.h"
#include "udt/ticker.h"
#include "udt/udt_error.h"
#include "udt/udp_hp_clt.h"
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

//begin udt_test_recvfile
static void udt_handle_receive(const boost::system::error_code& e, size_t bytes_received)
{
	if (e == udt_error::disconnected)
	{
		boost::system::error_code e;
		udt_sk.close(e);
		return;
	}
	fwrite(rcv_buf, sizeof(rcv_buf[0]), bytes_received, fpDest);
	if (e == udt_error::eof)
	{
		LOG_INFO("finished receive");
		milli_tk.toc();
		LOG_VAR(milli_tk.duration());
		fclose(fpDest);	//relay eof to our file.
		fpDest = NULL;
		udt_sk.async_shutdown(handle_shutdown);
	}
	else if(!e)
	{
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)),
			udt_handle_receive);
	}
	else
	{
		BOOST_ASSERT(false);
	}
}

static void udt_handle_accept(const boost::system::error_code& e, const udp_endpoint_t& ep)
{
	LOG_VAR(e);
	if (!e)
	{
		milli_tk.tic();
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, _countof(rcv_buf)), udt_handle_receive);
	}
}

void udt_test_recvfile(const char* filename, const char* ip, unsigned short port)
{
	init_udt_socket();
	udt_sk.async_accept(udp_endpoint_t(ip::address_v4::from_string(ip), port), udt_handle_accept);
	fpDest = fopen(filename, "wb");
	ios.run();
}
//end udt_test_recvfile

//begin tcp_test_recvfile
static void tcp_handle_receive(const boost::system::error_code& e, size_t bytes_received)
{
	if (bytes_received)
	{
		fwrite(rcv_buf, sizeof(rcv_buf[0]), bytes_received, fpDest);
	}
	if (!e)
	{
		tcp_sk.async_receive(buffer(rcv_buf, _countof(rcv_buf)), tcp_handle_receive);
	}
	else
	{
		fclose(fpDest);
		tcp_sk.close();
		milli_tk.toc();
		LOG_VAR(milli_tk.duration());
	}
}

static void tcp_handle_connect(const boost::system::error_code& e)
{
	if (!e)
	{
		tcp_sk.async_receive(buffer(rcv_buf, _countof(rcv_buf)), tcp_handle_receive);
	}
}

void tcp_test_recvfile(const char* filename)
{
	tcp_sk.open(ip::tcp::v4());
	tcp_sk.bind(ip::tcp::endpoint(ip::address_v4::any(), SERVER_PORT));
	tcp_sk.async_connect(ip::tcp::endpoint(ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT),
		tcp_handle_connect);
	fpDest = fopen(filename, "wb");
	milli_tk.tic();
	ios.run();
}
//end tcp_test_recvfile

//begin test_passive_connect
static void handle_passive_receive(const boost::system::error_code& e, size_t bytes_received)
{
	LOG_VAR(e);
	if (e)
	{
		boost::system::error_code e;
		udt_sk.close(e);
	}
}

static void handle_passive_connect(const boost::system::error_code& e, const udp_endpoint_t& )
{
	LOG_VAR(e);
	if (!e)
	{
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, _countof(rcv_buf)), handle_passive_receive);
	}
}

void test_passive_connect()
{
	init_udt_socket();
	udt_sk.async_accept(udp_endpoint_t(ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT), handle_passive_connect);
	ios.run();
}
//end test_passive_connect

//begin test_duplex_sendmsg
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
		udt_sk.close(e);
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
		udt_sk.close(e);
		return;
	}
	fwrite(rcv_buf, sizeof(rcv_buf[0]), byte_received, fpDest);
	if (e == udt_error::eof)
	{
		LOG_INFO("finished receive");
		fclose(fpDest);	//relay eof to our file.
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

static void handle_duplex_accept(const boost::system::error_code& e, const udp_endpoint_t& ep)
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
		LOG_VAR(read);
		if (read < sizeof(snd_buf))
		{
			udt_sk.async_shutdown(handle_shutdown);
		}
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)),
			handle_duplex_receive);
	}
}

void test_duplex_file_server()
{
	init_udt_socket();
	udt_sk.async_accept(udp_endpoint_t(ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT), handle_duplex_accept);
	fpSrc = fopen("c:\\msgdb.dat", "rb");
	fpDest = fopen("c:\\a.dat", "wb");
	ios.run();
}
//end test_duplex_file_server

//begin test_keepalive_server
static void handle_keepalive_accept(const boost::system::error_code& e, const udp_endpoint_t& ep)
{
	LOG_VAR(e);
	if (!e)
	{
		;	//do nothing.
	}
}

void test_keepalive_server()
{
	init_udt_socket();
	udt_sk.async_accept(udp_endpoint_t(ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT), handle_keepalive_accept);
	ios.run();
}
//end test_keepalive_server

//begin test_persist_server
static void handle_persist_receive(const boost::system::error_code& e, size_t byte_received)
{
	static int buf_size = 1024;
	buf_size --;
	if (buf_size <= 0)
	{
		buf_size = 0;
		static int count = 10;
		if (--count <= 0)
		{
			count = 10;
			buf_size = 1024;
		}
	}
	LOG_VAR(buf_size);
	udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, buf_size), handle_persist_receive);
}

static void handle_persist_accept(const boost::system::error_code& e, const udp_endpoint_t& ep)
{
	LOG_VAR(e);
	if (!e)
	{
		udt_sk.async_receive(udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)), handle_persist_receive);
	}
}
void test_persist_server()
{
	init_udt_socket();
	udt_sk.async_accept(udp_endpoint_t(ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT), handle_persist_accept);
	ios.run();
}
//end test_persist_server

//begin test_hole_punch_server
static void handle_hole_punch(const boost::system::error_code& e, const udp_endpoints_t& eps)
{
	LOG_VAR(e);
	for (udp_endpoints_t::const_iterator i = eps.begin(); i != eps.end(); ++i)
	{
		LOG_VAR(*i);
		udt_test_recvfile("e:\\msgdb.dat", i->address().to_v4().to_string().c_str(), i->port());
	}
}

void test_hole_punch_server()
{
	udp_hp_clt_default clt(ios);
	init_udt_socket();
	clt.create(&raw_sk);
	clt.async_hole_punch(handle_hole_punch);
	ios.run();
}
//end test_hole_punch_server

//begin test_udt_engine_server
static iudt_engine* engine = iudt_engine::get_instance();
static udt_engine_socket* sock = NULL;

static void handle_udt_engine_shutdown(const boost::system::error_code& e)
{
	LOG_VAR(e);
	engine->close(sock);
}

//begin udt_test_recvfile
static void udt_handle_udt_engine_receive(const boost::system::error_code& e, size_t bytes_received)
{
	if (e == udt_error::disconnected)
	{
		engine->close(sock);
		sock = NULL;
		return;
	}
	fwrite(rcv_buf, sizeof(rcv_buf[0]), bytes_received, fpDest);
	if (e == udt_error::eof)
	{
		LOG_INFO("finished receive");
		milli_tk.toc();
		LOG_VAR(milli_tk.duration());
		fclose(fpDest);	//relay eof to our file.
		fpDest = NULL;
		engine->async_shutdown(sock, handle_udt_engine_shutdown);
	}
	else if(!e)
	{
		engine->async_receive(sock,
			udt_socket::mutable_buffer_t(rcv_buf, sizeof(rcv_buf)),
			udt_handle_udt_engine_receive);
	}
	else
	{
		BOOST_ASSERT(false);
	}
}

void handle_udt_engine_connect(const boost::system::error_code& e)
{
	LOG_VAR(e);
	if (!e)
	{
		milli_tk.tic();
		fpDest = fopen("c:\\a.rar", "wb");
		engine->async_receive(sock,
			udt_socket::mutable_buffer_t(rcv_buf, _countof(rcv_buf)), udt_handle_udt_engine_receive);
	}
}

void test_udt_engine_server()
{
	sock = engine->create(SERVER_PORT);
	engine->async_connect(sock,
		udp_endpoint_t(boost::asio::ip::address_v4::from_string(CLIENT_IP), CLIENT_PORT),
		handle_udt_engine_connect);
}
//end test_udt_engine_server

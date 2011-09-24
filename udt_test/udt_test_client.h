#pragma once

#define CLIENT_PORT 1291
#define CLIENT_IP "172.16.3.212"

void udt_test_sendfile(const char* filename, const char* ip, unsigned short port);

void tcp_test_sendfile(const char* filename);

void test_active_connect();

void test_duplex_file_client();

void test_keepalive_client();

void test_persist_client();

void test_hole_punch_client();

void test_pmtu_client();

void test_udp_chknat();

void test_udp_pub_addr();

void test_udt_engine();

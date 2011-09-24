#pragma once

#define SERVER_PORT 1290
#define SERVER_IP "172.16.1.100"

void udt_test_recvfile(const char* filename, const char* ip, unsigned short port);

void tcp_test_recvfile(const char* filename);

void test_passive_connect();

void test_duplex_file_server();

void test_keepalive_server();

void test_persist_server();

void test_hole_punch_server();

void test_udt_engine_server();

#include "udt_defs.h"
#include <WinSock2.h>

void udt_hdr::hton()
{
	uh_win = htons(uh_win);
	uh_len = htons(uh_len);
	uh_off = htons(uh_off);
	uh_seq = htonl(uh_seq);
	uh_ack = htonl(uh_ack);
	uh_pseq = htonl(uh_pseq);
}

void udt_hdr::ntoh()
{
	uh_win = ntohs(uh_win);
	uh_len = ntohs(uh_len);
	uh_off = ntohs(uh_off);
	uh_seq = ntohl(uh_seq);
	uh_ack = ntohl(uh_ack);
	uh_pseq = ntohl(uh_pseq);
}

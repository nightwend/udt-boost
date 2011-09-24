#pragma once

typedef unsigned short u_short;

typedef unsigned long u_long;

typedef unsigned char u_char;

typedef u_long udt_seq;
//defines for the packet type code in an ETHERNET header
#define ETHER_TYPE_IP (0x0800)
#define ETHER_TYPE_8021Q (0x8100)

struct ip {
	u_char	ip_hl:4,		/* header length */
			ip_v:4;			/* version */
	u_char	ip_tos;			/* type of service */
	short	ip_len;			/* total length */
	u_short	ip_id;			/* identification */
	short	ip_off;			/* fragment offset field */
#define	IP_DF 0x4000			/* dont fragment flag */
#define	IP_MF 0x2000			/* more fragments flag */
#define	IP_OFFMASK 0x1fff		/* mask for fragmenting bits */
	u_char	ip_ttl;			/* time to live */
	u_char	ip_p;			/* protocol */
	u_short	ip_sum;			/* checksum */
	struct	in_addr ip_src,ip_dst;	/* source and dest address */
};

struct udphdr {
	u_short	uh_sport;		/* source port */
	u_short	uh_dport;		/* destination port */
	short	uh_ulen;		/* udp length */
	u_short	uh_sum;			/* udp checksum */
};

struct udt_hdr	//16 bytes
{
	enum flag_t
	{
		TH_FIN = 0x01,
		TH_SYN = 0x02,
		TH_RST = 0x04,
		TH_PUSH = 0x08,
		TH_ACK = 0x10,
		TH_URG = 0x20,
		TH_SACK = 0x40,
	};

	enum 
	{
		MAX_HEADER_SIZE = 16 + 40,	//attention 16 ==sizeof(udt_hdr)
	};

	u_char uh_ver;			/* version */
	u_char uh_flags;
	u_short uh_win;			/* window */

	u_short uh_len;			/* data length */
	u_short uh_off;			/* offset to data */

	udt_seq uh_seq;			/* sequence number */

	udt_seq uh_ack;			/* acknowledgement number */
};

struct udt_packet
{
	udt_hdr hdr;
	char data_ptr[1];
};

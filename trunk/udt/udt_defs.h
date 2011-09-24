
#pragma once

typedef unsigned short u_short;

typedef unsigned long u_long;

typedef unsigned char u_char;

typedef u_long udt_seq;

inline bool seq_lt (udt_seq lop ,udt_seq rop){return (int)(lop - rop) < 0;}	//modular comparative.

inline bool seq_le (udt_seq lop ,udt_seq rop){return (int)(lop - rop) <= 0;}

inline bool seq_gt (udt_seq lop ,udt_seq rop){return (int)(lop - rop) > 0;}

inline bool seq_ge (udt_seq lop ,udt_seq rop){return (int)(lop - rop) >= 0;}

struct udt_hdr	//20 bytes
{
	enum flag_t
	{
		TH_FIN = 0x01,
		TH_SYN = 0x02,
		TH_RST = 0x04,
		TH_PUSH = 0x08,
		TH_ACK = 0x10,
		TH_SACKED = 0x20,
		TH_RETRANS = 0x40,
		TH_HP = 0x80,	//hole punch flag.
	};

	enum 
	{
		MAX_HEADER_SIZE = 20 + 40,	//attention 20 ==sizeof(udt_hdr)
		MAX_ALL_HEADERS_SIZE = 20 + 8 + MAX_HEADER_SIZE,
		MIN_ALL_HEADERS_SIZE = 20 + 8 + 20,
	};

	void hton();

	void ntoh();

	u_char uh_ver;			/* version */
	u_char uh_flags;
	u_short uh_win;			/* window */

	u_short uh_len;			/* data length */
	u_short uh_off;			/* offset to data */

	udt_seq uh_seq;			/* sequence number */

	udt_seq uh_ack;			/* acknowledgement number */
	udt_seq uh_pseq;	//peer's sequence number if exists.
};

struct udt_packet
{
	enum packet_state_t
	{
		TCPCB_SACKED_ACKED = 0x01,	/* SKB ACK'd by a SACK block	*/
		TCPCB_SACKED_RETRANS = 0x02,	/* SKB retransmitted		*/
		TCPCB_LOST = 0x04,	/* SKB is lost			*/
		TCPCB_TAGBITS = 0x07,	/* All tag bits			*/

		TCPCB_EVER_RETRANS = 0x80,	/* Ever retransmitted frame	*/
		TCPCB_RETRANS = (TCPCB_SACKED_RETRANS|TCPCB_EVER_RETRANS),
	};

	udt_hdr hdr;
	char* data_ptr(){return (char*)this + hdr.uh_off;}
};

inline bool has_syn(u_char flags)
{
	return (flags & udt_hdr::TH_SYN) != 0;
}

inline bool has_fin(u_char flags)
{
	return (flags & udt_hdr::TH_FIN) != 0;
}

inline bool has_ack(u_char flags)
{
	return (flags & udt_hdr::TH_ACK) != 0;
}

inline bool has_rst(u_char flags)
{
	return (flags & udt_hdr::TH_RST) != 0;
}

inline bool has_rexmt(u_char flags)
{
	return (flags & udt_hdr::TH_RETRANS) != 0;
}

inline bool has_hp(u_char flags)
{
	return (flags & udt_hdr::TH_HP) != 0;
}

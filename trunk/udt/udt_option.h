#pragma once

/*
	Author:	gongyiling@myhada.com
	Data:	2011-6-20
*/
#include <utility>
#include "udt_defs.h"
#include "udt_buf.h"

#pragma pack(1)
struct udt_option
{
	enum options_t
	{
		/*
		 *	TCP option
		 */
		TCPOPT_NOP = 1,	/* Padding */
		TCPOPT_EOL = 0,	/* End of options */
		TCPOPT_MSS = 2,	/* Segment size negotiating */
		TCPOPT_WINDOW = 3,	/* Window scaling */
		TCPOPT_SACK_PERM = 4,       /* SACK Permitted */
		TCPOPT_SACK = 5,       /* SACK Block */
		TCPOPT_CWND = 6,
	};
	
	u_char kind;

	u_char len;

protected:

	static u_char* padding_4bytes(u_char* end_option);
};

struct udt_option_sack : public udt_option
{
	enum
	{
		MAX_PAIR_SEQS = 3,
	};

	typedef std::pair<udt_seq, udt_seq> pair_seq_t;

	typedef udt_option base_type;

	udt_option_sack();

	size_t compose(udt_buf::list_packets_const_riter_t b,
		udt_buf::list_packets_const_riter_t e, size_t remain_len);

	size_t size() const {return (len - sizeof(base_type)) / sizeof(pair_seqs[0]);}

	void ntoh();

	void hton();

	pair_seq_t pair_seqs[MAX_PAIR_SEQS];	//max 3 pairs, maybe less.
};

struct udt_option_cwnd : public udt_option
{
	u_long cwnd;
};

#pragma pack()

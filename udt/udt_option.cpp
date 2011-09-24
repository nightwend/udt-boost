#include "udt_option.h"
#include <WinSock2.h>
#include <boost/assert.hpp>
#include "udt_log.h"

u_char* udt_option::padding_4bytes(u_char* end_option)
{
	int to_pad = (int)end_option & (int)0x03;
	while (to_pad-- > 0)
	{
		*end_option++ = TCPOPT_NOP;
	}
	return end_option;
}

udt_option_sack::udt_option_sack()
{
	kind = TCPOPT_SACK;
	len = sizeof(base_type);
}

void check_sack_option(udt_option_sack* opt_sack)
{
	size_t i = opt_sack->size();
	for(size_t j = 0; j < i; j++)
	{
		BOOST_ASSERT(seq_lt(opt_sack->pair_seqs[j].first, opt_sack->pair_seqs[j].second));
		if (j != i - 1)
		{
			BOOST_ASSERT(seq_gt(opt_sack->pair_seqs[j].second, opt_sack->pair_seqs[j+1].first));
		}
	}
}

size_t udt_option_sack::compose(udt_buf::list_packets_const_riter_t b,
								udt_buf::list_packets_const_riter_t e,
								size_t remain_len)
{
	if (b == e ||
		remain_len < sizeof(base_type) + sizeof(pair_seqs[0]))
	{
		return remain_len;
	}
	remain_len -= sizeof(base_type);
	size_t i = 0;
	udt_hdr* ti = get_hdr(b);
	udt_seq prev_seq = ti->uh_seq;
	udt_seq prev_end_seq = prev_seq + ti->uh_len;
	b++;
	while(b != e && remain_len >= sizeof(pair_seqs[0]) && i < MAX_PAIR_SEQS)
	{
		udt_hdr* ti = get_hdr(b);
		if (prev_seq != ti->uh_seq + ti->uh_len)	//gap detected.
		{
			LOG_VAR_C(prev_seq);
			LOG_VAR_C(prev_end_seq);
			pair_seqs[i].first = prev_seq;
			pair_seqs[i].second = prev_end_seq;
			prev_end_seq = ti->uh_seq + ti->uh_len;
			remain_len -= sizeof(pair_seqs[0]);
			i++;
		}
		prev_seq = ti->uh_seq;
		b++;
	}
	if (remain_len >= sizeof(pair_seqs[0]) && i < MAX_PAIR_SEQS)
	{
		LOG_VAR_C(prev_seq);
		LOG_VAR_C(prev_end_seq);
		pair_seqs[i].first = prev_seq;
		pair_seqs[i].second = prev_end_seq;
		remain_len -= sizeof(pair_seqs[0]);
		i++;
	}
	len = sizeof(base_type) + sizeof(pair_seqs[0]) * i;
	kind = TCPOPT_SACK;
#ifdef _DEBUG
	check_sack_option(this);
#endif
	return remain_len;
}

void udt_option_sack::ntoh()
{
	size_t i = size();
	for(size_t j = 0; j < i; j++)
	{
		pair_seqs[j].first = ntohl(pair_seqs[j].first);
		pair_seqs[j].second = ntohl(pair_seqs[j].second);
	}
}

void udt_option_sack::hton()
{
	size_t i = size();
	for(size_t j = 0; j < i; j++)
	{
		pair_seqs[j].first = htonl(pair_seqs[j].first);
		pair_seqs[j].second = htonl(pair_seqs[j].second);
	}
}

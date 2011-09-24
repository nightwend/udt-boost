#include "udt_buf.h"
#include "udt_defs.h"
#include <new>
#include <memory.h>
#include <algorithm>
#include <boost/assert.hpp>
#include <utility>
#include "udt_log.h"

udt_buf::udt_buf():
m_cc(0),
m_hiwat(DEFAULT_BUFFER_SIZE),
m_lowat(DEFAULT_LOWAT)
{

}

udt_buf::~udt_buf()
{
	clear();
}

void udt_buf::clear()
{
	free(m_pkts);
	m_pkts.clear();
	m_cc = 0;
}

udt_packet* udt_buf::alloc(size_t data_size)
{
	udt_packet* pkt = (udt_packet*)new char[sizeof(udt_hdr) + data_size];
	if (pkt != NULL)
	{
		udt_hdr* ti = &(pkt->hdr);
		ti->uh_len = data_size;
		ti->uh_off = sizeof(udt_hdr);
		ti->uh_flags = 0;
		ti->uh_seq = 0;
		ti->uh_ack = 0;
		ti->uh_win = 0;
		memset((char*)pkt + sizeof(udt_hdr), 0xaf, data_size);
	}
	return pkt;
}

void udt_buf::free(udt_packet* pkt)
{
	delete [] (char*)pkt;
}

void udt_buf::free(list_packets_t& pkts)
{
	for (list_packets_iter_t i = pkts.begin(); i != pkts.end(); ++i)
	{
		free(*i);
	}
}

size_t udt_buf::copy(char* buf, size_t buf_len, bool erase_copied)
{
	if (m_pkts.empty() || buf_len == 0 || buf == NULL)
	{
		return 0;
	}
	list_packets_iter_t b = m_pkts.begin();
	list_packets_iter_t e = m_pkts.end();
	udt_seq seq = (*b)->hdr.uh_seq;
	size_t total_copied = 0;
	do
	{
		udt_packet* pkt = get_pkt(b);
		udt_hdr* hdr = get_hdr(b);
		size_t to_copy = std::min<size_t>(hdr->uh_len, buf_len);
		
		memcpy(total_copied + buf, pkt->data_ptr(), to_copy);
		
		if (buf_len < hdr->uh_len)	//do partial copy
		{
			hdr->uh_off += to_copy;
			hdr->uh_seq += to_copy;
			hdr->uh_len -= to_copy;
			erase_copied = false;
		}

		total_copied += to_copy;
		buf_len -= to_copy;
		if (erase_copied)
		{
			free(pkt);
			b = m_pkts.erase(b);
		}
		else
		{
			++b;
		}
		seq += to_copy;
	}while(b != e && seq == (*b)->hdr.uh_seq && buf_len > 0);
	m_cc -= total_copied;
	return total_copied;
}

bool udt_buf::append(const char* data, size_t data_len, size_t seg_size)
{
	if (data_len > acc() || data_len == 0 || data == NULL || seg_size == 0)
	{
		return false;
	}
	m_cc += data_len;
	while (data_len > 0)
	{
		size_t pkt_len = std::min<size_t>(data_len, seg_size);
		udt_packet* pkt = alloc(pkt_len);
		udt_hdr* hdr = &(pkt->hdr);
		hdr->uh_seq = m_seq;
		m_seq += pkt_len;
		memcpy(pkt->data_ptr(), data, pkt_len);
		m_pkts.push_back(pkt);
		data_len -= pkt_len;
		data += pkt_len;
	}
	return true;
}

bool udt_buf::append(udt_packet* pkt)
{
	size_t data_len = pkt->hdr.uh_len;
	if (data_len > acc())
	{
		return false;
	}
	m_cc += data_len;
	m_pkts.push_back(pkt);
	return true;
}

void check_buffer_size(udt_buf::list_packets_t pkts, size_t cc)
{
	for (udt_buf::list_packets_iter_t i = pkts.begin(); i != pkts.end(); ++i)
	{
		cc -= get_hdr(i)->uh_len;
	}
	BOOST_ASSERT(cc == 0);
}

udt_buf::list_packets_iter_t udt_buf::drop(list_packets_iter_t i)
{
	m_cc -= get_hdr(i)->uh_len;
	free(get_pkt(i));
	list_packets_iter_t ret = erase(i);
#ifdef _DEBUG
	check_buffer_size(m_pkts, m_cc);
#endif
	return ret;
}

bool udt_buf::fin() const
{
	if (!m_pkts.empty())
	{
		return has_fin(m_pkts.back()->hdr.uh_flags);
	}
	else
	{
		return false;
	}
}

udt_snd_buf::udt_snd_buf()
{

}

udt_snd_buf::~udt_snd_buf()
{
	
}

udt_rcv_buf::udt_rcv_buf()
{
	
}

udt_rcv_buf::~udt_rcv_buf()
{
	clear();
}

void udt_rcv_buf::clear()
{
	base_type::clear();
	free(m_reasm_pkts);
	m_reasm_pkts.clear();
}

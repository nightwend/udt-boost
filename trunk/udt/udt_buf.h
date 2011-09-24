#pragma once


/************************************************************************/
/*
	Author:	gongyiling@myhada.com
	Data:	not remembered.
*/
/************************************************************************/
#include <list>
#include "udt_defs.h"
struct udt_packet;

class udt_buf
{
public:

	enum
	{
		DEFAULT_BUFFER_SIZE = 32 * 1024,
		DEFAULT_LOWAT = 1,
	};

	typedef std::list<udt_packet*> list_packets_t;

	typedef list_packets_t::iterator list_packets_iter_t;

	typedef list_packets_t::const_reverse_iterator list_packets_const_riter_t;

	udt_buf();
	
	virtual ~udt_buf();

	void clear();

	static udt_packet* alloc(size_t data_size);	//分配数据包内存

	static void free(udt_packet* pkt);	//释放数据包内存

	static void free(list_packets_t& pkts);

	size_t copy(char* buf, size_t buf_len, bool erase_copied);

	bool append(const char* data, size_t data_len, size_t seg_size);

	bool append(udt_packet* pkt);

	list_packets_iter_t drop(list_packets_iter_t i);

	size_t cc() const {return m_cc;}

	size_t hiwat() const {return m_hiwat;}
	
	size_t lowat() const {return m_lowat;}

	size_t acc() const {return hiwat() - cc();}

	bool above_lowat() const {return m_cc >= m_lowat;}

	list_packets_iter_t begin(){return m_pkts.begin();}

	list_packets_iter_t end(){return m_pkts.end();}

	bool fin() const;//判断m_pkts的最后一个是不是fin

	list_packets_iter_t erase(list_packets_iter_t b, list_packets_iter_t e){return m_pkts.erase(b, e);}

	list_packets_iter_t erase(list_packets_iter_t i){return m_pkts.erase(i);}

	void set_init_seq(udt_seq seq){m_seq = seq;}

	udt_seq seq() const {return m_seq;}

	void set_hiwat(size_t hw){m_hiwat = hw;}

protected:

	size_t m_cc;	//buffer中数据和
	size_t m_hiwat;	//最多存储数据
	size_t m_lowat;	//低水位数据

	list_packets_t m_pkts;	//按序列号升序排列的数据包

	udt_seq m_seq;
#ifdef _DEBUG
	friend class udt_cb;
#endif
};

inline udt_packet* get_pkt(udt_buf::list_packets_iter_t i){return (*i);}

inline udt_hdr* get_hdr(udt_buf::list_packets_iter_t i){return &((*i)->hdr);}

inline udt_packet* get_pkt(udt_buf::list_packets_const_riter_t i){return (*i);}

inline udt_hdr* get_hdr(udt_buf::list_packets_const_riter_t i){return &((*i)->hdr);}

class udt_snd_buf : public udt_buf
{
public:

	typedef udt_buf base_type;

	udt_snd_buf();

	virtual ~udt_snd_buf();
};

class udt_rcv_buf : public udt_buf
{
public:

	typedef udt_buf base_type;

	udt_rcv_buf();

	virtual ~udt_rcv_buf();

	udt_buf::list_packets_t& reasm_pkts(){return m_reasm_pkts;}

	void clear();

private:

	udt_buf::list_packets_t m_reasm_pkts;
};

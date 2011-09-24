// udt_analyzer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
//  pcap_throughput
//
//   reads in a pcap file and outputs basic throughput statistics 

#include <stdio.h>
#include <stdlib.h>
#include <pcap.h>
#include "udt_defs.h"
#pragma comment(lib,"ws2_32.lib")

void usage()
{
	printf("udt_analyzer filename\n");
}
//------------------------------------------------------------------- 
int main(int argc, char **argv) 
{ 

	unsigned long max_volume = 0;  //max value of bytes in one-second interval 
	unsigned long current_ts=0; //current timestamp 

	//temporary packet buffers 
	struct pcap_pkthdr header; // The header that pcap gives us 
	const u_char *packet; // The actual packet 

	//-------- Begin Main Packet Processing Loop ------------------- 
	//loop through each pcap file in command line args 

	//----------------- 
	//open the pcap file 
	pcap_t *handle; 
	char errbuf[PCAP_ERRBUF_SIZE]; //not sure what to do with this, oh well 
	if (argc != 2)
	{
		usage();
		return -1;
	}
	handle = pcap_open_offline(argv[1], errbuf);   //call pcap library function 

	//----------------- 
	//begin processing the packets in this particular file, one at a time 

	while (packet = pcap_next(handle,&header)) { 
		// header contains information about the packet (e.g. timestamp) 
		u_char *pkt_ptr = (u_char *)packet; //cast a pointer to the packet data 

		//parse the first (ethernet) header, grabbing the type field 
		int ether_type = ((int)(pkt_ptr[12]) << 8) | (int)pkt_ptr[13]; 
		int ether_offset = 0; 

		if (ether_type == ETHER_TYPE_IP) //most common 
			ether_offset = 14; 
		else if (ether_type == ETHER_TYPE_8021Q) //my traces have this 
			ether_offset = 18; 
		
		printf("seconds: %d\nuseconds: %f\n", header.ts.tv_sec, header.ts.tv_usec / 1000.f);
		//parse the IP header 
		pkt_ptr += ether_offset;  //skip past the Ethernet II header 
		ip *ip_hdr = (ip *)pkt_ptr; //point to an IP header structure 
		udphdr* udp_hdr = (udphdr*)(pkt_ptr + sizeof(ip));
		udt_hdr* udthdr = (udt_hdr*)(pkt_ptr + sizeof(ip) + sizeof(udphdr));
		printf("src: %s:%d\n", inet_ntoa(ip_hdr->ip_src), ntohs(udp_hdr->uh_sport));
		printf("dst: %s:%d\n", inet_ntoa(ip_hdr->ip_dst), ntohs(udp_hdr->uh_dport));

		/*	u_char uh_ver;
		u_char uh_flags;
		u_short uh_win;	

		u_short uh_len;	
		u_short uh_off;

		udt_seq uh_seq;

		udt_seq uh_ack;
		*/
		printf("uh_ver: %d\n", udthdr->uh_ver);

		const static char* flags[] = 
		{
			"TH_FIN",
			"TH_SYN",
			"TH_RST",
			"TH_PUSH",
			"TH_ACK",
			"TH_URG",
			"TH_SACK"
		};
		printf("uh_flags: ");
		bool prev_has = false;
		for (int i = 0; i < _countof(flags); i++)
		{
			if ((udthdr->uh_flags >> i) & 0x1)
			{
				if (!prev_has)
				{
					prev_has = true;
					printf("%s", flags[i]);
				}
				else
				{
					printf(", %s", flags[i]);
				}
			}
		}
		printf("\n");
		
		printf("uh_win: %d\n", ntohs(udthdr->uh_win));
		printf("uh_len: %d\n", ntohs(udthdr->uh_len));
		printf("uh_off: %d\n", ntohs(udthdr->uh_off));
		printf("uh_seq: %d\n", ntohl(udthdr->uh_seq));
		printf("uh_ack: %d\n", ntohl(udthdr->uh_ack));
		printf("\n");
	} //end internal loop for reading packets (all in one file) 

	pcap_close(handle);  //close the pcap file 

	
	return 0; //done
} //end of main() function

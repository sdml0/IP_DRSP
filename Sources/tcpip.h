#pragma once
#include "enc28j60.h"

#define IP_PKT(x) ((ip_packet_t*)((uint8_t*)(x) + sizeof(eth_frame_t)))
#define TCP_PKT(x) ((tcp_packet_t*)((uint8_t*)(x) + sizeof(eth_frame_t) + sizeof(ip_packet_t)))
#define tcp_head_size(tcp)	(((tcp)->data_offset & 0xf0) >> 2)
#define tcp_get_data(tcp)	((uint8_t*)(tcp) + tcp_head_size(tcp))
#define FULL_HEAD_SIZE (sizeof(eth_frame_t) + sizeof(ip_packet_t) + sizeof(tcp_packet_t))

#define IP_PACKET_TTL				64
//#define TCP_MAX_CONNECTIONS		8
#define TCP_WINDOW_SIZE				65535
#define TCP_SYN_MSS					512
//#define TCP_CONN_TIMEOUT			10		// sec

#define ntohs(a)        ((((a)>>8)&0xff)|(((a)<<8)&0xff00))
#define inet_addr(a,b,c,d)	(((uint32_t)a) | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24))

#define ETH_TYPE_ARP			ntohs(0x0806)
#define ETH_TYPE_IP			ntohs(0x0800)

#define ARP_HW_TYPE_ETH		ntohs(0x0001)
#define ARP_PROTO_TYPE_IP	ntohs(0x0800)
#define ARP_TYPE_REQUEST	ntohs(1)
#define ARP_TYPE_RESPONSE	ntohs(2)

#define IP_PROTOCOL_ICMP	1
#define IP_PROTOCOL_TCP		6
#define IP_PROTOCOL_UDP		17

#define ICMP_TYPE_ECHO_RQ	8
#define ICMP_TYPE_ECHO_RPLY	0

#define TCP_FLAG_URG		0x20
#define TCP_FLAG_ACK		0x10
#define TCP_FLAG_PSH		0x08
#define TCP_FLAG_RST		0x04
#define TCP_FLAG_SYN		0x02
#define TCP_FLAG_FIN		0x01


typedef __packed struct eth_frame {
	uint8_t to_addr[6];
	uint8_t from_addr[6];
	uint16_t type;
	uint8_t data[];
} eth_frame_t;

typedef __packed struct arp_message {
	uint16_t hw_type;
	uint16_t proto_type;
	uint8_t hw_addr_len;
	uint8_t proto_addr_len;
	uint16_t type;
	uint8_t mac_addr_from[6];
	uint32_t ip_addr_from;
	uint8_t mac_addr_to[6];
	uint32_t ip_addr_to;
} arp_message_t;

typedef __packed struct arp_cache_entry {
	uint32_t ip_addr;
	uint8_t mac_addr[6];
} arp_cache_entry_t;

typedef __packed struct ip_packet {
	uint8_t ver_head_len;
	uint8_t tos;
	uint16_t total_len;
	uint16_t fragment_id;
	uint16_t flags_framgent_offset;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t cksum;
	uint32_t from_addr;
	uint32_t to_addr;
	uint8_t data[];
} ip_packet_t;

typedef __packed struct icmp_echo_packet {
	uint8_t type;
	uint8_t code;
	uint16_t cksum;
	uint16_t id;
	uint16_t seq;
	uint8_t data[];
} icmp_echo_packet_t;

typedef __packed struct tcp_packet {
	uint16_t from_port;
	uint16_t to_port;
	uint32_t seq_num;
	uint32_t ack_num;
	uint8_t data_offset;
	uint8_t flags;
	uint16_t window;
	uint16_t cksum;
	uint16_t urgent_ptr;
	uint8_t data[];
} tcp_packet_t;

typedef enum tcp_status_code {
	TCP_CLOSED,
	TCP_SYN_SENT,
	TCP_SYN_RECEIVED,
	TCP_ESTABLISHED,
	TCP_FIN_WAIT
} tcp_status_code_t;

typedef struct tcp_state {  	// __packed
	uint32_t event_time;
	uint32_t seq_num;
	uint32_t ack_num;
	uint32_t remote_addr;
	uint16_t remote_port;
	uint16_t local_port;
			uint32_t ack_recived;
			uint8_t dup_ack_num;
} tcp_state_t;

typedef enum tcp_sending_mode {
	TCP_SENDING_SEND,
	TCP_SENDING_REPLY,
	TCP_SENDING_RESEND
} tcp_sending_mode_t;

//#define TCP_OPTION_PUSH			0x01
//#define TCP_OPTION_CLOSE		0x02
//extern uint8_t net_buf[];

void lan_init(void);
void tcp_write(int16_t data_len);
void tcp_xmit(eth_frame_t *frame, uint16_t data_len);
void process_packet(void);
void get_fw(void);
void tcp_filter(void);
void icmp_filter(void);
void arp_filter(uint8_t* buf);

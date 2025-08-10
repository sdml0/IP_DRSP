#include "tcpip.h"

#define COPY_MAC(to, from) *(uint32_t*)(to) = *(uint32_t*)(from); *((uint16_t*)(to) + 2) = *((uint16_t*)(from) + 2)
#define ARP_PKT(x) ((arp_message_t*)((uint8_t*)(x) + sizeof(eth_frame_t)))
#define ICMP_PKT(x) ((icmp_echo_packet_t*)((uint8_t*)(x) + sizeof(eth_frame_t) + sizeof(ip_packet_t)))
#define tcp_listen(x) (TCP_PKT(x)->to_port == ntohs(80))
#define _FRAME ((eth_frame_t*)net_buf)
#define _FRAME_FW ((eth_frame_t*)net_buf_fw)
#define net_buf_fw (net_buf + sizeof(net_buf) - ENC28J60_MAXFRAME)
#define FOR_TCP 4224

// Packet buffer
uint8_t net_buf[FOR_TCP] __attribute__((section("net_buf")));

extern const uint8_t raw_200_data[74];
extern const uint8_t webif_404_reply[275];
extern uint8_t mac_addr[6];
uint32_t ip_addr;
uint16_t conn_counter;

uint32_t parse_POST(uint8_t **req);

uint16_t ip_cksum(uint32_t sum, uint8_t* buf, uint16_t len)
{
	sum = __rev(sum);
	while(len >= 2) {
		sum += *(uint16_t*)buf;
		buf += 2;
		len -= 2;
	}
	if(len) sum += *buf;
	while(sum >> 16) sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

void tcp_xmit(eth_frame_t *frame, uint16_t len)
{
	uint32_t temp;

	if (IP_PKT(frame)->to_addr == ip_addr) {		// move to filter ?
		temp = TCP_PKT(frame)->from_port;
		TCP_PKT(frame)->from_port = TCP_PKT(frame)->to_port;
		TCP_PKT(frame)->to_port = temp;
		TCP_PKT(frame)->data_offset = sizeof(tcp_packet_t) << 2;
		TCP_PKT(frame)->window = ntohs(TCP_WINDOW_SIZE);
		TCP_PKT(frame)->urgent_ptr = 0;

		IP_PKT(frame)->fragment_id = 0;
		IP_PKT(frame)->flags_framgent_offset = 0;
		IP_PKT(frame)->ttl = IP_PACKET_TTL;
		IP_PKT(frame)->to_addr = IP_PKT(frame)->from_addr;
		IP_PKT(frame)->from_addr = ip_addr;

		COPY_MAC(frame->to_addr, frame->from_addr);
		COPY_MAC(frame->from_addr, mac_addr);
		temp = TCP_PKT(frame)->seq_num;
		TCP_PKT(frame)->seq_num = TCP_PKT(frame)->ack_num;
		TCP_PKT(frame)->ack_num = temp;
	}

	TCP_PKT(frame)->cksum = 0;
	TCP_PKT(frame)->cksum = ip_cksum(len + sizeof(tcp_packet_t) + IP_PROTOCOL_TCP, (uint8_t*)TCP_PKT(frame) - 8, len + sizeof(tcp_packet_t) + 8);
	
	IP_PKT(frame)->total_len = ntohs(len + sizeof(tcp_packet_t) + sizeof(ip_packet_t));
	IP_PKT(frame)->cksum = 0;
	IP_PKT(frame)->cksum = ip_cksum(0, (void*)IP_PKT(frame), sizeof(ip_packet_t));
	
	enc28j60_send_packet((uint8_t*)frame, len + sizeof(tcp_packet_t) + sizeof(ip_packet_t) + sizeof(eth_frame_t));
	
	TCP_PKT(frame)->seq_num = __rev(__rev(TCP_PKT(frame)->seq_num) + len);
}


void tcp_filter()
{
	uint16_t len;

	//static uint32_t temp = IP_PKT(_FRAME)->from_addr;
	
	len = ntohs(IP_PKT(_FRAME)->total_len) - tcp_head_size(TCP_PKT(_FRAME)) - sizeof(ip_packet_t);			// user data length
	TCP_PKT(_FRAME)->flags &= TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_RST | TCP_FLAG_FIN;						// process only SYN/FIN/ACK/RST
		
	if(TCP_PKT(_FRAME)->flags == TCP_FLAG_SYN) {
		TCP_PKT(_FRAME)->ack_num = (conn_counter++ << 16) | RTC->CNTL;
		TCP_PKT(_FRAME)->seq_num = __rev(__rev(TCP_PKT(_FRAME)->seq_num) + 1);
		TCP_PKT(_FRAME)->flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
		tcp_xmit(_FRAME, 0);
		return;
	}

	if (!(TCP_PKT(_FRAME)->flags & TCP_FLAG_ACK) || (TCP_PKT(_FRAME)->flags & TCP_FLAG_RST)) return; 	// We need only ACK here
		
	
	if (len > 5) {
		TCP_PKT(_FRAME)->seq_num = __rev(__rev(TCP_PKT(_FRAME)->seq_num) + len);
		tcp_write(len);							// feed data to app
	}
	else if (len == 1) {		// Keep-Alive
		TCP_PKT(_FRAME)->seq_num = __rev(__rev(TCP_PKT(_FRAME)->seq_num) + 1);
		TCP_PKT(_FRAME)->flags = TCP_FLAG_ACK;
		tcp_xmit(_FRAME, 0);
	}
	else if (TCP_PKT(_FRAME)->flags & TCP_FLAG_FIN) {		// FIN
		TCP_PKT(_FRAME)->seq_num = __rev(__rev(TCP_PKT(_FRAME)->seq_num) + 1);
		TCP_PKT(_FRAME)->flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
		tcp_xmit(_FRAME, 0);		
	}
}

void icmp_filter()
{
	if(ICMP_PKT(_FRAME)->type == ICMP_TYPE_ECHO_RQ) {
		ICMP_PKT(_FRAME)->type = ICMP_TYPE_ECHO_RPLY;
		ICMP_PKT(_FRAME)->cksum += 8;									// update cksum
		
		IP_PKT(_FRAME)->fragment_id = 0;
		IP_PKT(_FRAME)->flags_framgent_offset = 0;
		IP_PKT(_FRAME)->ttl = IP_PACKET_TTL;
		IP_PKT(_FRAME)->to_addr = IP_PKT(_FRAME)->from_addr;
		IP_PKT(_FRAME)->from_addr = ip_addr;
		IP_PKT(_FRAME)->cksum = 0;
		IP_PKT(_FRAME)->cksum = ip_cksum(0, (void*)IP_PKT(_FRAME), sizeof(ip_packet_t));

		COPY_MAC(_FRAME->to_addr, _FRAME->from_addr);
		COPY_MAC(_FRAME->from_addr, mac_addr);
		enc28j60_send_packet(net_buf, ntohs(IP_PKT(_FRAME)->total_len) + sizeof(eth_frame_t));

	}
}

void arp_filter(uint8_t* buf)
{
	if((ARP_PKT(buf)->hw_type == ARP_HW_TYPE_ETH) && (ARP_PKT(buf)->proto_type == ARP_PROTO_TYPE_IP)
			&& (ARP_PKT(buf)->ip_addr_to == ip_addr) && (ARP_PKT(buf)->type == ARP_TYPE_REQUEST)) {
		ARP_PKT(buf)->type = ARP_TYPE_RESPONSE;
		COPY_MAC(ARP_PKT(buf)->mac_addr_to, ARP_PKT(buf)->mac_addr_from);
		COPY_MAC(ARP_PKT(buf)->mac_addr_from, mac_addr);
		ARP_PKT(buf)->ip_addr_to = ARP_PKT(buf)->ip_addr_from;
		ARP_PKT(buf)->ip_addr_from = ip_addr;
		
		COPY_MAC(((eth_frame_t*)buf)->to_addr, ((eth_frame_t*)buf)->from_addr);
		COPY_MAC(((eth_frame_t*)buf)->from_addr, mac_addr);
		enc28j60_send_packet(buf, sizeof(arp_message_t) + sizeof(eth_frame_t));
	}
}

void lan_init()
{
	// RTC Init must be !!
	GPIOB->CRL &= ~0xF0000000;
	GPIOB->BSRR = 1<<7;
	GPIOB->CRL |= 8u<<28; 	// PB 07 - eth int, input+pull up
	
	enc28j60_init(mac_addr);
}

void process_packet()
{
	if (enc28j60_recv_packet(net_buf)) {
															//GPIOB->BSRR = 1<<07;										// for debug
		
		if(_FRAME->type == ETH_TYPE_ARP) arp_filter(net_buf);
		else if ((_FRAME->type == ETH_TYPE_IP) && (IP_PKT(_FRAME)->ver_head_len == 0x45)
		&& (IP_PKT(_FRAME)->to_addr == ip_addr)) {
			if (IP_PKT(_FRAME)->protocol == IP_PROTOCOL_ICMP)	icmp_filter();
			else if ((IP_PKT(_FRAME)->protocol == IP_PROTOCOL_TCP) && tcp_listen(_FRAME)) tcp_filter();
		}
															//GPIOB->BSRR = 1<<(07+16);								// for debug
	}
}

void get_fw()
{
	uint8_t* req;
	int16_t len;
	static uint8_t* pt_;
	static uint16_t sz_;
	static uint32_t ip_, seq_;
	
	if (!enc28j60_recv_packet(net_buf_fw)) return;
	
	if(_FRAME_FW->type == ETH_TYPE_ARP) arp_filter(net_buf_fw);
	else if ((_FRAME_FW->type == ETH_TYPE_IP) && (IP_PKT(_FRAME_FW)->ver_head_len == 0x45)
	&& (IP_PKT(_FRAME_FW)->to_addr == ip_addr) && (IP_PKT(_FRAME_FW)->protocol == IP_PROTOCOL_TCP)
						&& tcp_listen(_FRAME_FW)) {
		
		len = ntohs(IP_PKT(_FRAME_FW)->total_len) - tcp_head_size(TCP_PKT(_FRAME_FW)) - sizeof(ip_packet_t);
		if (len < 2) return;
							
		req = tcp_get_data(TCP_PKT(net_buf_fw));		
		if (sz_) {						// POST Continues
			if((ip_ == IP_PKT(net_buf_fw)->from_addr) && (seq_ == TCP_PKT(net_buf_fw)->seq_num)) {
				mem_cpy(pt_, req, len);
				pt_ += len;
				seq_ = __rev(__rev(TCP_PKT(_FRAME_FW)->seq_num) + len);
				TCP_PKT(_FRAME_FW)->seq_num = seq_;
				sz_ -= len;
						
				if (!sz_) {
					req = TCP_PKT(net_buf_fw)->data;
					mem_cpy(req, raw_200_data, sizeof(raw_200_data) );
					tcp_xmit(_FRAME_FW, sizeof(raw_200_data));
					SCB->AIRCR = 0x05FA0004;
				}
				else tcp_xmit(_FRAME_FW, 0);						
			}
		}
		else if ((*(uint32_t*)req == 0x54534F50) && (*(uint32_t*)(req + 4) == 0x663F2F20) && (*(uint32_t*)(req + 8) == 0x70755F77)) { 	// POST + " /?fw_up"
			pt_ = req + 12;
			sz_ = parse_POST(&pt_);
			seq_ = __rev(__rev(TCP_PKT(_FRAME_FW)->seq_num) + len);		// It will be ACK
			TCP_PKT(_FRAME_FW)->seq_num = seq_;
			len -= pt_ - req;
			if ((len > 0) && (sz_ <= 18432) && (*(uint32_t*)pt_ == 0x70555766)) {		// "fWUp"
				mem_cpy((uint8_t*)0x20000000, pt_, len);
				pt_ = (uint8_t*)0x20000000 + len;
				ip_ = IP_PKT(net_buf_fw)->from_addr;
				sz_ -= len;
				tcp_xmit(_FRAME_FW, 0);
			}
			else {
				sz_ = 0;
				req = TCP_PKT(net_buf_fw)->data;
				mem_cpy(req, webif_404_reply, sizeof(webif_404_reply));
				tcp_xmit(_FRAME_FW, sizeof(webif_404_reply));				
			}
		}		// reboot ?
		else if ((*(uint32_t*)req == 0x20544547) && (*(uint32_t*)(req + 4) == 0x65723F2F) && (*(uint32_t*)(req + 8) == 0x746F6F62)) {
			req = TCP_PKT(net_buf_fw)->data;
			mem_cpy(req, raw_200_data, sizeof(raw_200_data));
			tcp_xmit(_FRAME_FW, sizeof(raw_200_data));
		}
	}
}


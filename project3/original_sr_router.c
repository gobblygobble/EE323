/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>


#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/
void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */



/*---------------------------------------------------------------------
 Check source ip with ip_blacklist
 Goal 1 : check whether the source ip is black ip or not
 Goal 2 : Print Log 
	- Format  :  " [Source ip blocked] : <source ip> "
		e.g.) [Source ip blocked] : 10.0.2.100
*/
int ip_black_list(struct sr_ip_hdr* iph)
{
	int blk = 0;
	char ip_blacklist[20]="10.0.2.0";
	char mask[20]="255.255.255.0";
	/**************** fill in code here *****************/
	/****************************************************/
	return blk;
}
/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/
void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{

  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

/*  printf("*** -> Received packet of length %d \n",len);*/

  /* fill in code here */
	uint8_t *new_pck;							/* new packet */
	unsigned int new_len;						/* length of new_pck */

	unsigned int len_r;							/* length remaining, for validation */
	uint16_t checksum;							/* checksum, for validation */

	struct sr_ethernet_hdr *e_hdr0, *e_hdr;		/* Ethernet headers */
	struct sr_ip_hdr *i_hdr0, *i_hdr;			/* IP headers */
	struct sr_arp_hdr *a_hdr0, *a_hdr;			/* ARP headers */
	struct sr_icmp_hdr *ic_hdr0;				/* ICMP header */
	struct sr_icmp_t3_hdr *ict3_hdr;			/* ICMP type3 header */

	struct sr_if *ifc;							/* router interface */
	uint32_t ipaddr;							/* IP address */
	struct sr_rt *rtentry;						/* routing table entry */
	struct sr_arpentry *arpentry;				/* ARP table entry in ARP cache */
	struct sr_arpreq *arpreq;					/* request entry in ARP cache */
	struct sr_packet *en_pck;					/* encapsulated packet in ARP cache */

	/* validation */
	if (len < sizeof(struct sr_ethernet_hdr))
		return;
	len_r = len - sizeof(struct sr_ethernet_hdr);
	e_hdr0 = (struct sr_ethernet_hdr *) packet;		/* e_hdr0 set */

	/* IP packet arrived */
	if (e_hdr0->ether_type == htons(ethertype_ip)) {

		/* validation */
		if (len_r < sizeof(struct sr_ip_hdr))
			return;
		len_r = len_r - sizeof(struct sr_ip_hdr);
		i_hdr0 = (struct sr_ip_hdr *) (((uint8_t *) e_hdr0) + sizeof(struct sr_ethernet_hdr));		/* i_hdr0 set */
		if (i_hdr0->ip_v != 0x4)
			return;
		checksum = i_hdr0->ip_sum;
		i_hdr0->ip_sum = 0;
		if (checksum != cksum(i_hdr0, sizeof(struct sr_ip_hdr)))
			return;
		i_hdr0->ip_sum = checksum;

		/* check destination */
		for (ifc = sr->if_list; ifc != NULL; ifc = ifc->next)
			if (i_hdr0->ip_dst == ifc->ip){
				break;}


		/* check ip black list */
		if(ip_black_list(i_hdr0)){
			/* Drop the packet */
			return;	
		}

		/* destined to router interface */
		if (ifc != NULL) {

			/* with ICMP */
			if (i_hdr0->ip_p == ip_protocol_icmp) {

				/* validation */
				if (len_r < sizeof(struct sr_icmp_hdr))
					return;
				ic_hdr0 = (struct sr_icmp_hdr *) (((uint8_t *) i_hdr0) + sizeof(struct sr_ip_hdr));		/* ic_hdr0 set */

				/* echo request type */
				if (ic_hdr0->icmp_type == 0x08) {

					/* validation */
					checksum = ic_hdr0->icmp_sum;
					ic_hdr0->icmp_sum = 0;
					if (checksum != cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr)))
						return;
					ic_hdr0->icmp_sum = checksum;

					/* modify to echo reply */
					i_hdr0->ip_ttl = INIT_TTL;
					ipaddr = i_hdr0->ip_src;
					i_hdr0->ip_src = i_hdr0->ip_dst;
					i_hdr0->ip_dst = ipaddr;
					i_hdr0->ip_sum = 0;
					i_hdr0->ip_sum = cksum(i_hdr0, sizeof(struct sr_ip_hdr));
					ic_hdr0->icmp_type = 0x00;
					ic_hdr0->icmp_sum = 0;
					ic_hdr0->icmp_sum = cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));
					rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
					if (rtentry != NULL) {
						ifc = sr_get_interface(sr, rtentry->interface);
						memcpy(e_hdr0->ether_shost, ifc->addr, ETHER_ADDR_LEN);
						arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
						if (arpentry != NULL) {
							memcpy(e_hdr0->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
							free(arpentry);
							/* send */
							sr_send_packet(sr, packet, len, rtentry->interface);
						}
						else {
							/* queue */
							arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, packet, len, rtentry->interface);
							sr_arpcache_handle_arpreq(sr, arpreq);
						}
					}

					/* done */
					return;
				}

				/* other types */
				else
					return;
			}

			/* with TCP or UDP */
			else if (i_hdr0->ip_p == ip_protocol_tcp || i_hdr0->ip_p == ip_protocol_udp) {

				/* validation */
				if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE)
					return;


				/**************** fill in code here *****************/
				/* generate ICMP port unreachable packet */
				new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr);
				new_pck = (uint8_t *) calloc(1, new_len);
				
				
				/* send */
						
				

				/* queue */


				

				/*****************************************************/
				/* done */
				free(new_pck);
				return;
			}

			/* with others */
			else
				return;
		}

		
		/* destined elsewhere, forward */
		else {
			/* refer routing table */
			rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
				
			/* hit */
			if (rtentry != NULL) {
			
			/**************** fill in code here *****************/		
			/* check TTL expiration */
					if () {
						/* TTL expired */		

			/* validation */
						
						

			/* generate ICMP time exceeded packet */



			/* send */
		
			/* queue */

			/*****************************************************/
						/* done */
						free(new_pck);
						return;
				}
			/**************** fill in code here *****************/	

				/* set src MAC addr */

				/* refer ARP table */

				/* hit */
				if (arpentry != NULL) {
				/* set dst MAC addr */

				/* decrement TTL */

				/* forward */

				/*****************************************************/
				}
				/* miss */
				else {
					/* queue */
					arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, packet, len, rtentry->interface);
					sr_arpcache_handle_arpreq(sr, arpreq);
				}	
				/* done */
				return;
			}

			/* miss */
			else {
				/**************** fill in code here *****************/	

				/* validation */

				/* generate ICMP net unreachable packet */
					
				/* send */

				/* queue */

				/*****************************************************/

				/* done */
				free(new_pck);
				return;
			}
		}
	}

	/* ARP packet arrived */
	else if (e_hdr0->ether_type == htons(ethertype_arp)) {
		
		/* validation */
		if (len_r < sizeof(struct sr_arp_hdr))
			return;
		a_hdr0 = (struct sr_arp_hdr *) (((uint8_t *) e_hdr0) + sizeof(struct sr_ethernet_hdr));		/* a_hdr0 set */
		
		/* destined to me */
		ifc = sr_get_interface(sr, interface);
		if (a_hdr0->ar_tip == ifc->ip) {

			/* request code */
			if (a_hdr0->ar_op == htons(arp_op_request)) {
			/**************** fill in code here *****************/			
			/* generate reply */
	



			/* send */
			/*****************************************************/
				/* done */
				free(new_pck);
				return;
			}

			/* reply code */
			else if (a_hdr0->ar_op == htons(arp_op_reply)) {
				/**************** fill in code here *****************/	
				
				/* pass info to ARP cache */
				
				/* pending request exist */

				/* set dst MAC addr */


				/* decrement TTL except for self-generated packets */

				/* send */

				/* done */

				/*****************************************************/
				/* no exist */
				else
					return;
			}

			/* other codes */
			else
				return;
		}

		/* destined to others */
		else
			return;
	}

	/* other packet arrived */
	else
		return;


}/* end sr_ForwardPacket */
struct sr_rt *sr_findLPMentry(struct sr_rt *rtable, uint32_t ip_dst)
{
	struct sr_rt *entry, *lpmentry = NULL;
	uint32_t mask, lpmmask = 0;

	ip_dst = ntohl(ip_dst);

	/* scan routing table */
	for (entry = rtable; entry != NULL; entry = entry->next) {
		mask = ntohl(entry->mask.s_addr);
		/* longest match so far */
		if ((ip_dst & mask) == (ntohl(entry->dest.s_addr) & mask) && mask > lpmmask) {
				lpmentry = entry;
				lpmmask = mask;
		}
	}

	return lpmentry;
}



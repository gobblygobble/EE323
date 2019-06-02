/*
 * transport.c 
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

/* my headers */
#include <arpa/inet.h>
#include <stdbool.h>

/* my macros */
#define WINDOW_SIZE 3072

enum {
    CSTATE_ESTABLISHED,
    CSTATE_LISTEN,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RCVD,
    // WISH TO CLOSE
    CSTATE_FIN_WAIT1,   // Sent FIN
    CSTATE_FIN_WAIT2,   // Waiting for FIN
    CSTATE_CLOSING,     // Waiting for FIN-ACK
    // ASKED TO CLOSE
    CSTATE_CLOSE_WAIT,  // CLOSE and send FIN
    CSTATE_LAST_ACK,    // Waiting for FIN-ACK
    CSTATE_CLOSED,
    };    /* obviously you should have more states */

enum PacketType {
    SYN,
    ACK,
    SYNACK,
    DATA,
    FIN,
    FINACK,
};
typedef enum PacketType PacketType;

/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    /* any other connection-wide global variables go here */
    /* my variables */
    // already sent
    tcp_seq prev_seq;
    tcp_seq prev_ack;
    size_t prev_len;
    // received
    tcp_seq rcvd_seq;
    tcp_seq rcvd_ack;
    size_t rcvd_len;
    tcp_seq rcvd_win;
    // to be sent
    tcp_seq next_seq;
    // windows
    uint32_t cwnd;
    uint32_t swnd;
    uint32_t remainder_window;
    // log file pointer
    FILE *logfile;
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);


/* My functions start */
STCPHeader* CreatePacket(tcp_seq seqnum, tcp_seq acknum, PacketType type, char* payload, size_t length);
bool SendPacket(mysocket_t sd, context_t* ctx, PacketType type, char* src, size_t src_len);
bool WaitPacket(mysocket_t sd, context_t* ctx, PacketType type);
void PrintPacket(STCPHeader *packet, bool isSend);
/* My functions end */

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);

    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */

    /* initialize cwnd, swnd, remainder_window and connection_state*/
    ctx->cwnd = STCP_MSS;
    ctx->swnd = STCP_MSS;
    ctx->remainder_window = STCP_MSS;
    ctx->connection_state = CSTATE_LISTEN;

    if (is_active) {
	    // send SYN & change state
        if (!SendPacket(sd, ctx, SYN, NULL, 0)) {
            perror("3-way handshake send SYN");
            free(ctx);
            return;
        }
        ctx->connection_state = CSTATE_SYN_SENT;
        if (!WaitPacket(sd, ctx, SYNACK)) {
            perror("3-way handshake wait SYNACK");
            free(ctx);
            return;
        }
        ctx->connection_state = CSTATE_ESTABLISHED;
        if (!SendPacket(sd, ctx, ACK, NULL, 0)) {
            perror("3-way handshake send ACK");
            free(ctx);
            return;
        }
        ctx->logfile = fopen("client_log.txt", "w");
    }
	else {
        // wait for SYN packet to arrive
        if (!WaitPacket(sd, ctx, SYN)) {
            perror("3-way handshake wait SYN");
            free(ctx);
            return;
        }
        if (!SendPacket(sd, ctx, SYNACK, NULL, 0)) {
            perror("3-way handshake send SYNACK");
            free(ctx);
            return;
        }
        ctx->connection_state = CSTATE_SYN_RCVD;
        if (!WaitPacket(sd, ctx, ACK)) {
            perror("3-way handshake wait ACK");
            free(ctx);
            return;
        }
        ctx->connection_state = CSTATE_ESTABLISHED;
        ctx->logfile = fopen("server_log.txt", "w");
    }

    stcp_unblock_application(sd);

    control_loop(sd, ctx);
    
    /* do any cleanup here */
    fclose(ctx->logfile);
    free(ctx);
}


/* generate initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);
    ctx->initial_sequence_num = 1;
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);

    unsigned int event;
    char *buffer;
    size_t max_length = 0;
    ssize_t data_length = 0;

    fprintf(stdout, "control_loop() called.\n");

    while (!ctx->done)
    {

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        max_length = (ctx->remainder_window > STCP_MSS) ? STCP_MSS : ctx->remainder_window;
        //max_length -= sizeof(STCPHeader);
        
        buffer = (char *)calloc(1, STCP_MSS + sizeof(STCPHeader));
        STCPHeader *packet = (STCPHeader *)buffer;

        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA) {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            data_length = stcp_app_recv(sd, buffer, max_length);

            if (data_length == 0) {
                // something wrong
                fprintf(stderr, "control_loop(): Supposed to get APP_DATA but received nothing.\n");
                free(buffer);
                free(ctx);
                return;
            }
            if (!SendPacket(sd, ctx, DATA, buffer, data_length)) {
                perror("control_loop(): Sending DATA");
                free(buffer);
                free(ctx);
                return;
            }
            if (!WaitPacket(sd, ctx, ACK)) {
                perror("control_loop(): Waiting for ACK of sent DATA");
                free(buffer);
                free(ctx);
                return;
            }
        }
        
        else if (event & NETWORK_DATA) {
            /* incoming data from the peer */
            fprintf(stdout, "Now I can receive up to %lu bytes.\n", max_length);
            data_length = stcp_network_recv(sd, (void *)buffer, max_length + sizeof(STCPHeader));

            if (data_length < (ssize_t)sizeof(STCPHeader)) {
                // something wrong
                fprintf(stderr, "control_loop(): Supposed to get NETWORK_DATA but received something too small.\n");
                free(buffer);
                free(ctx);
                return;
            }
            
            PrintPacket((STCPHeader *)buffer, 0);
            fprintf(stdout, "Received data length: %lu.\n", data_length);
            
            /* check for all possible closing cases */
            if ((ctx->connection_state == CSTATE_ESTABLISHED) && (packet->th_flags & TH_FIN)) {
                // was open -> asked to close: 4-way handshake
                // TODO: maybe change ACK to FINACK?
                if (!SendPacket(sd, ctx, ACK, NULL, 0)) {
                    perror("4-way handshake send FINACK 1");
                    free(buffer);
                    free(ctx);
                    return;
                }
                ctx->connection_state = CSTATE_CLOSE_WAIT;
                // notify upper layer
                stcp_fin_received(sd);
                if (!SendPacket(sd, ctx, FIN, NULL, 0)) {
                    perror("4-way handshake send FIN 2");
                    free(buffer);
                    free(ctx);
                    return;
                }
                ctx->connection_state = CSTATE_LAST_ACK;
                // TODO: maybe change ACK to FINACK?
                if (!WaitPacket(sd, ctx, ACK)) {
                    perror("4-way handshake wait FINACK 2");
                    free(buffer);
                    free(ctx);
                    return;
                }
                ctx->connection_state = CSTATE_CLOSED;
                ctx->done = 1;
                break;
            }
            
            else {
                // regular data
                // TODO: do something with data
                fprintf(stdout, "2\n");
                stcp_app_send(sd, ((char *)buffer + sizeof(STCPHeader)), ((size_t)data_length - sizeof(STCPHeader)));

                // set ctx fields for next ACK
                STCPHeader *datapacket = (STCPHeader *)buffer;
                ctx->rcvd_seq = ntohl(datapacket->th_seq);
                ctx->rcvd_ack = ntohl(datapacket->th_ack);
                ctx->rcvd_len = (size_t)data_length - sizeof(STCPHeader);
                ctx->rcvd_win = ntohl(datapacket->th_win);

                //fprintf(stdout, "Do something with data packet.\n");
                // send ACK
                if (!SendPacket(sd, ctx, ACK, NULL, 0)) {
                    perror("control_loop(): Sending ACK of received DATA");
                    free(buffer);
                    free(ctx);
                    return;
                }
            }
        }
        else if (event & APP_CLOSE_REQUESTED) {
            /* the socket asked to be closed */
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                fprintf(stderr, "control_loop(): App reqeuested close but already in process.\n");
                free(buffer);
                free(ctx);
                assert(0);
                return;
            }
            // nothing wrong
            //stcp_fin_received(sd);
            if (!SendPacket(sd, ctx, FIN, NULL, 0)) {
                perror("control_loop(): 4-way handshake send FIN 1");
                free(buffer);
                free(ctx);
                return;
            }
            ctx->connection_state = CSTATE_FIN_WAIT1;
            // TODO: maybe change ACK to FINACK?
            if (!WaitPacket(sd, ctx, ACK)) {
                perror("control_loop(): 4-way handshake wait FINACK 1");
                free(buffer);
                free(ctx);
                return;
            }
            ctx->connection_state = CSTATE_FIN_WAIT2;
            if (!WaitPacket(sd, ctx, FIN)) {
                perror("control_loop(): 4-way handshake wait FIN 2");
                free(buffer);
                free(ctx);
                return;
            }
            // TODO: maybe change ACK to FINACK?
            if (!SendPacket(sd, ctx, ACK, NULL, 0)) {
                perror("control_loop(): 4-way handshake send FINACK 2");
                free(buffer);
                free(ctx);
                return;
            }
            ctx->connection_state = CSTATE_CLOSED;
            ctx->done = 1;
            break;
        }

        else if (event & TIMEOUT) {
            /* timeout */
            fprintf(stderr, "control_loop: Didn't expect TIMEOUT event...\n");
            assert(0);
        }

        else {
            // should not reach here?
            fprintf(stderr, "control_loop(): Unknown event type.\n");
            assert(0);
        }
    }
    /* clean up my mess! */
    free(buffer);
    return;
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}

/**********************************************************************/
/* CreatePacket
 *
 * Creates a MEMORY-ALLOCATED packet of the specified type and returns it.
 * Called in SendPacket()
 */
STCPHeader*
CreatePacket(tcp_seq seqnum, tcp_seq acknum, PacketType type, char* payload, size_t length)
{
    STCPHeader *header = (STCPHeader *)calloc(1, sizeof(STCPHeader) + length);
    header->th_seq = htonl(seqnum);
    header->th_ack = htonl(acknum);
    header->th_off = 5;  // no options whatsoever
    header->th_win = htons(WINDOW_SIZE);
    switch (type)
    {
        case SYN:
            header->th_flags = TH_SYN;
            break;
        case ACK:
            header->th_flags = TH_ACK;
            break;
        case SYNACK:
            header->th_flags = TH_SYN | TH_ACK;
            break;
        case FIN:
            header->th_flags = TH_FIN;
            break;
        case FINACK:
            header->th_flags = TH_FIN | TH_ACK;
            break;
        case DATA:
            assert(payload);
            assert(length);
            header->th_flags = 0x0;
            memcpy((void *)header + sizeof(STCPHeader), payload, length);
            break;
        default:
            fprintf(stderr, "CreatePacket(): Unknown packet type\n");
            assert(0);
            break;
    }
    return header;
}

/**********************************************************************/
/* SendPacket
 *
 * Sends a packet of the specified type.
 * Returns true on success and false on error.
 * TODO:
 * Not sure if errno should be set.
 * Not certain on the sequence number updates of ctx.
 */
bool
SendPacket(mysocket_t sd, context_t* ctx, PacketType type, char* src, size_t src_len)
{
    // variables
    STCPHeader *packet;
    tcp_seq seqnum, acknum;
    ssize_t numBytes;

    switch (type)
    {
        case SYN:
            seqnum = ctx->initial_sequence_num;
            acknum = 0;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = 1;
            break;
        case ACK:
            //seqnum = ctx->rcvd_ack;
            seqnum = ctx->next_seq;
            acknum = ctx->rcvd_seq + ctx->rcvd_len;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = 1;
            break;
        case SYNACK:
            seqnum = ctx->initial_sequence_num;
            acknum = ctx->rcvd_seq + 1;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = 1;
            break;
        case FIN:
            seqnum = ctx->next_seq;
            acknum = ctx->prev_ack;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = 1;
            break;
        case FINACK:
            // probably no such type
            seqnum = ctx->rcvd_ack;
            acknum = ctx->rcvd_seq + 1;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = 1;
            break;
        case DATA:
            assert(src);
            assert(src_len);
            // print log
            if (ctx->connection_state == CSTATE_ESTABLISHED) {
                fprintf(ctx->logfile, "Send:\t%u\t%u\t%lu\n", ctx->swnd, ctx->remainder_window, src_len);
            }
            seqnum = ctx->next_seq;
            acknum = ctx->prev_ack;
            ctx->prev_seq = seqnum;
            ctx->prev_ack = acknum;
            ctx->prev_len = src_len;

            ctx->remainder_window -= src_len;
            fprintf(stdout, "Sending %lu bytes of data (not including header).\n", src_len);
            break;
        default:
            fprintf(stderr, "SendPacket(): Unknown packet type.\n");
            return false;
            break;
    }
    packet = CreatePacket(seqnum, acknum, type, src, src_len);
    numBytes = stcp_network_send(sd, (void *)packet, sizeof(STCPHeader) + src_len, NULL);
    PrintPacket(packet, true);
    
    free(packet);
    
    if (numBytes > 0) {
        return true;
    }
   
    fprintf(stderr, "SendPacket(): stcp_network_send(): non-positive sent packet.\n");
    return false;
}

/**********************************************************************/
/* WaitPacket
 *
 * Waits for a packet of the specified type.
 * Returns true on success and false on any failure.
 * This function should not ever be called in control loop.
 * Hence, it only waits for "NETWORK_DATA".
 * TODO:
 * rcvd_win setting
 * rcvd_len setting
 */
bool
WaitPacket(mysocket_t sd, context_t *ctx, PacketType type)
{
    STCPHeader *packet = (STCPHeader *)calloc(1, sizeof(STCPHeader) + STCP_MSS);
    unsigned int event;
    ssize_t numBytes;

    event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);

    if (!(event & NETWORK_DATA)) {
        fprintf(stderr, "WaitPacket(): Expected NETWORK_DATA(2), got %d instead.\n", event);
    }

    numBytes = stcp_network_recv(sd, (void *)packet, sizeof(STCPHeader) + STCP_MSS);

    if (numBytes < (ssize_t)sizeof(STCPHeader)) {
        // something went wrong
        free(packet);
        return false;
    }

    switch (type)
    {
        case SYN:
            if ((packet->th_flags & TH_SYN) == TH_SYN) {
                ctx->rcvd_seq = ntohl(packet->th_seq);
                ctx->rcvd_ack = ntohl(packet->th_ack);
                ctx->rcvd_win = ntohs(packet->th_win);
                ctx->rcvd_len = (size_t)numBytes - sizeof(STCPHeader);
                
                ctx->rcvd_len = (ctx->rcvd_len == 0) ? 1 : ctx->rcvd_len;

                ctx->next_seq = ctx->initial_sequence_num; // not used
            }
            else {
                fprintf(stderr, "WaitPacket(): Waiting for SYN... Unexpected packet type.\n");
                free(packet);
                return false;
            }
            break;
        case ACK:
            if ((packet->th_flags & TH_ACK) == TH_ACK) {
                
                // print log
                if (ctx->connection_state == CSTATE_ESTABLISHED) {
                    fprintf(ctx->logfile, "Recv:\t%u\t%u\t%lu\n", ctx->swnd, ctx->remainder_window, ctx->prev_len);
                }
                
                ctx->rcvd_seq = ntohl(packet->th_seq);
                ctx->rcvd_ack = ntohl(packet->th_ack);
                ctx->rcvd_win = ntohs(packet->th_win);
                ctx->rcvd_len = (size_t)numBytes - sizeof(STCPHeader);
                
                ctx->rcvd_len = (ctx->rcvd_len == 0) ? 1 : ctx->rcvd_len;

                ctx->next_seq = ctx->rcvd_ack;
                
                uint32_t prev_swnd = ctx->swnd;
                if (ctx->prev_len != 1) {
                    if (ctx->cwnd < (4 * STCP_MSS)) ctx->cwnd += STCP_MSS;
                    else                            ctx->cwnd += (STCP_MSS * STCP_MSS / ctx->cwnd);
                }
                // set swnd to min(cwnd, rwnd == WINDOW_SIZE)
                ctx->swnd = (ctx->cwnd < WINDOW_SIZE) ? ctx->cwnd : WINDOW_SIZE;
                if (ctx->prev_len != 1) ctx->remainder_window += ctx->prev_len;
                ctx->remainder_window += (ctx->swnd - prev_swnd);
            }
            else {
                fprintf(stderr, "WaitPacket(): Waiting for ACK... Unexpected packet type.\n");
                free(packet);
                return false;
            }
            break;
        case SYNACK:
            if (((packet->th_flags & TH_SYN) | (packet->th_flags & TH_ACK)) == (TH_SYN | TH_ACK)) {
                ctx->rcvd_seq = ntohl(packet->th_seq);
                ctx->rcvd_ack = ntohl(packet->th_ack);
                ctx->rcvd_win = ntohs(packet->th_win);
                ctx->rcvd_len = (size_t)numBytes - sizeof(STCPHeader);

                ctx->rcvd_len = (ctx->rcvd_len == 0) ? 1 : ctx->rcvd_len;
                ctx->next_seq = ctx->rcvd_ack;
            }
            else {
                fprintf(stderr, "WaitPacket(): Waiting for SYNACK... Unexpected packet type.\n");
                free(packet);
                return false;
            }
            break;
        case FIN:
            if ((packet->th_flags & TH_FIN) == (TH_FIN)) {
                ctx->rcvd_seq = ntohl(packet->th_seq);
                ctx->rcvd_ack = ntohl(packet->th_ack);
                ctx->rcvd_win = ntohs(packet->th_win);
                ctx->rcvd_len = (size_t)numBytes - sizeof(STCPHeader);

                ctx->rcvd_len = (ctx->rcvd_len == 0) ? 1 : ctx->rcvd_len;
                ctx->next_seq = ctx->rcvd_ack;
            }
            else {
                fprintf(stderr, "WaitPacket(): Waiting for FIN... Unexpected packet type.\n");
                free(packet);
                return false;
            }
            break;
        default:
            fprintf(stderr, "WaitPacket(): Packet with unspecified behavior.\n");
            free(packet);
            return false;
            break;
    }
    //free(packet);
    return true;
}

/**********************************************************************/
/* PrintPacket
 *
 * Prints information of a packet.
 */
void PrintPacket(STCPHeader *packet, bool isSend)
{
    if (isSend) {
        fprintf(stdout, "----------------Packet Log (Send)----------------\n");
        fprintf(stdout, "S-port: %d, D-port:%d, SEQ: %u, ACK: %u, Offset: %d, Window: %d, Cksum: %d\n",
                    ntohs(packet->th_sport), ntohs(packet->th_dport), ntohl(packet->th_seq), ntohl(packet->th_ack),
                    packet->th_off, ntohs(packet->th_win), ntohs(packet->th_sum));
    }
    else {
        fprintf(stdout, "----------------Packet Log (Rcv)-----------------\n");
        fprintf(stdout, "S-port: %d, D-port:%d, SEQ: %u, ACK: %u, Offset: %d, Window: %d, Cksum: %d\n",
                    htons(packet->th_sport), htons(packet->th_dport), htonl(packet->th_seq), htonl(packet->th_ack),
                    packet->th_off, htons(packet->th_win), htons(packet->th_sum));
    }
    fprintf(stdout, "FLAGS: ");
    if (packet->th_flags & TH_FIN) fprintf(stdout, "FIN ");
    if (packet->th_flags & TH_SYN) fprintf(stdout, "SYN ");
    if (packet->th_flags & TH_ACK) fprintf(stdout, "ACK ");
    fprintf(stdout, "\n");
    fprintf(stdout, "-------------------------------------------------\n"); 
}

/**********************************************************************/
/* CheckPacket
 *
 * Checks if the given packet should be ignored or processed.
 * 
 * Returns true if it should be processed and
 * false if it should be ignored.
 */
bool CheckPacket(context_t *ctx, void* buffer)
{
    STCPHeader *header = (STCPHeader *)buffer;
    if (header->th_flags | TH_ACK) {
        fprintf(stdout, "\n");
    }

    return true;
}

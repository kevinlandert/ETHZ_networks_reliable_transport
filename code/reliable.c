#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <stdbool.h>

#include "rlib.h"
#include "buffer.h"

struct reliable_state {
	rel_t *next; /* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c; /* This is the connection object */

	/* Add your own data fields below this */
	// ...
	int N;
	int timeout;
	bool eof_r;

	//Sender fields
	buffer_t* send_buffer;
	// ...
	buffer_t* send_window;

	uint32_t nextseqno_send;
	uint32_t send_base;

	//Receiver fields
	buffer_t* rec_buffer;
	//
	buffer_t* rec_window;

	uint32_t rcv_base;

// ...

};
rel_t *rel_list;

/* Creates a new reliable protocol session, returns NULL on failure.
 * ss is always NULL */
rel_t *
rel_create(conn_t *c, const struct sockaddr_storage *ss,
		const struct config_common *cc) {
	rel_t *r;

	r = xmalloc(sizeof(*r));
	memset(r, 0, sizeof(*r));

	if (!c) {
		c = conn_create(r, ss);
		if (!c) {
			free(r);
			return NULL;
		}
	}

	r->c = c;
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list)
		rel_list->prev = &r->next;
	rel_list = r;

	/* Do any other initialization you need here... */
	// ...
	r->N = cc->window;
	r->timeout = cc->timeout;
	r->eof_r = false;

	r->send_buffer = xmalloc(sizeof(buffer_t));
	r->send_buffer->head = NULL;
	// ...
	r->send_window = xmalloc(sizeof(buffer_t)); //Packets sent waitingg for akg
	r->send_window->head = NULL;

	r->send_base = 1;
	r->nextseqno_send = 1;

	r->rec_buffer = xmalloc(sizeof(buffer_t));
	r->rec_buffer->head = NULL;
	// ...

	r->rec_window = xmalloc(sizeof(buffer_t));
	r->rec_window->head = NULL;

	r->rcv_base = 1;

	return r;
}

//Todo
void rel_destroy(rel_t *r) {
	if (r->next) {
		r->next->prev = r->prev;
	}
	*r->prev = r->next;
	conn_destroy(r->c);

	/* Free any other allocated memory here */
	buffer_clear(r->send_buffer);
	free(r->send_buffer);
	buffer_clear(r->rec_buffer);
	free(r->rec_buffer);
	// ...

	buffer_clear(r->send_window);

}

// n is the expected length of pkt
void rel_recvpkt(rel_t *r, packet_t *pkt, size_t n) {
	/* Your logic implementation here */

	//When a packet is received the library will call rel_recvpkt and supply you
	// with rel_t
	//Checks if packet corrupt
	if (pkt->cksum != cksum(pkt->data, n)) {
		;
	}
	else {
		//Sender
		if (n == 8) {

			//Traverse the sender window buffer, and set akg bit of received package to 1
			if (buffer_size(r->send_window) != 0) {
				buffer_node_t* element = buffer_get_first(r->send_window);
				while (element != NULL && element->packet.seqno <= pkt->seqno) {
					if (element->packet.seqno == pkt->seqno) {
						element->packet.ackno = 1;
						break;
					} else {
						element = element->next;
					}
				}
			}

			//Move window by removing all packets that have been akg in order
			if (pkt->seqno == r->send_base) {
				while (buffer_size(r->send_window) != 0
						&& buffer_get_first(r->send_window)->packet.ackno == 1) {
					buffer_remove_first(r->send_window);
					r->send_base = pkt->ackno + 1; //sets next packet we want akg from
				}
			}

			rel_read(r);

		}

		//Receiver

		//Packet from sender of length 12 means end of transfer
		else if (n == 12) {
			rel_destroy(r);
		}

		else {
			//Two cases: 1) is within window 2) is before window
			if (r->rcv_base <= pkt->seqno
					&& pkt->seqno <= r->rcv_base + r->N - 1) {

				//Check if allready received
				if (!buffer_contains(r->rec_buffer, pkt->seqno))
					buffer_insert(r->rec_buffer, pkt, 0); //last transmission time?

				//Create and send akg packet
				packet_t* packet = malloc(sizeof(packet_t));
				packet->cksum = 0; //?? what checksum in ak packacge
				packet->len = 8;
				packet->ackno = pkt->ackno;

				conn_sendpkt(r->c, packet, 8);

				rel_output(r);

			} else if (r->rcv_base - r->N <= pkt->seqno
					&& pkt->seqno <= r->rcv_base - 1) {

				//Create and send akg packet for previously akg paket
				packet_t* packet = malloc(sizeof(packet_t));
				packet->cksum = 0; //?? what checksum in ak packacge
				packet->len = 8;
				packet->ackno = pkt->ackno;

				conn_sendpkt(r->c, packet, 8);
			}

		}

	}

}

void rel_read(rel_t *s) {
	/* Your logic implementation here */

	//Call conn_input here which reads from standard input
	// If no data available conn_input will return 0
	// Library will call rel_read again (do not loop conn_input if it returns 0
	// simply return
	char buf[500];

	int nbytes = conn_input(s->c, buf, 500); //evt hier size

	if (nbytes == 0) {
		;
	}

	else if (nbytes == -1)
		s->eof_r = true;
	else {

		//We create packet
		packet_t* packet = malloc(sizeof(packet_t));
		packet->cksum = cksum(buf, nbytes);
		packet->len = 12 + nbytes;
		packet->ackno = s->send_base;
		packet->seqno = s->nextseqno_send;
		memcpy(packet->data,buf,strlen(buf)+1);   //könnte noch probleme mit type geben

		if (*buf == EOF)
			s->eof_r = true;

		struct timeval now;
		gettimeofday(&now, NULL);


		s->nextseqno_send++;

		//Insert data into input buffer
		buffer_insert(s->send_buffer, packet, -1); //We insert the packet in our buffer

		//If there is space in the send window, take packet from input buffer, send and put in sent buffer
		while (buffer_size(s->send_window) <= s->N
				&& buffer_size(s->send_buffer) != 0) {

			packet_t* packet = malloc(sizeof(packet_t));
			*packet = buffer_get_first(s->send_buffer)->packet;
			gettimeofday(&now, NULL);
			long last_retransmit = now.tv_sec * 1000 + now.tv_usec / 1000;
			conn_sendpkt(s->c, packet, packet->len);
			buffer_insert(s->send_window, packet, last_retransmit);
			buffer_remove_first(s->send_buffer);
		}

	}

}

void rel_output(rel_t *r) {
	/* Your logic implementation here */

	int sp_av = conn_bufspace(r->c); //Space available
	int nbytes = 0; //Amount outputed

	while (nbytes <= sp_av && buffer_size(r->rec_buffer) != 0
			&& buffer_get_first(r->rec_buffer)->packet.ackno == 1) {
		packet_t* packet = malloc(sizeof(packet_t));
		*packet = buffer_get_first(r->rec_buffer)->packet;
		int len = packet->len - 12;

		char buf[500];
		memcpy(buf,packet->data,strlen(packet->data)+1);

		//Only deliver if enough space
		if (nbytes + sizeof(buf) <= sp_av) {
			conn_output(r->c, buf, len);  //evt pointer
			buffer_remove_first(r->rec_buffer);
			r->rcv_base++; //Advance window
			nbytes += sizeof(buf);
		} else {
			break;
		}
	}
	if (buffer_size(r->rec_buffer) == 0 && r->eof_r) {
		rel_destroy(r);
	}

}

void rel_timer() {
	// Go over all reliable senders, and have them send out
	// all packets whose timer has expired
	rel_t *current = rel_list;
	while (current != NULL) {

		if (buffer_size(current->send_window) != 0) {
			buffer_node_t* element = malloc(sizeof(buffer_node_t));
			element = buffer_get_first(current->send_window);

			//Traverse all elements in sent buffer and resend certain ones
			while (element != NULL) {

				struct timeval now;
				gettimeofday(&now, NULL);
				long t = now.tv_sec * 1000 + now.tv_usec / 1000; //How we get clock

				//Resend packets that have not been akg after certain time
				if (element->last_retransmit + current->timeout < t
						&& element->packet.ackno == 0) {
					conn_sendpkt(current->c, &element->packet,
							element->packet.len);
					element->last_retransmit = t;
				}
				element = element->next;
			}
		}

		current = rel_list->next;
	}
}

/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The low level, platform independant network code
 *
 * =======================================================================
 */

#include <time.h>

#include "header/common.h"

/*
 * packet header
 * -------------
 * 31	sequence
 * 1	does this message contain a reliable payload
 * 31	acknowledge sequence
 * 1	acknowledge receipt of even/odd message
 * 16	qport
 *
 * The remote connection never knows if it missed a reliable message,
 * the local side detects that it has been dropped by seeing a sequence
 * acknowledge higher thatn the last reliable sequence, but without the
 * correct even/odd bit for the reliable set.
 *
 * If the sender notices that a reliable message has been dropped, it
 * will be retransmitted.  It will not be retransmitted again until a
 * message after the retransmit has been acknowledged and the reliable
 * still failed to get there.
 *
 * if the sequence number is -1, the packet should be handled without a
 * netcon
 *
 * The reliable message can be added to at any time by doing MSG_Write*
 * (&netchan->message, <data>).
 *
 * If the message buffer is overflowed, either by a single message, or
 * by multiple frames worth piling up while the last reliable transmit
 * goes unacknowledged, the netchan signals a fatal error.
 *
 * Reliable messages are always placed first in a packet, then the
 * unreliable message is included if there is sufficient room.
 *
 * To the receiver, there is no distinction between the reliable and
 * unreliable parts of the message, they are just processed out as a
 * single larger message.
 *
 * Illogical packet sequence numbers cause the packet to be dropped, but
 * do not kill the connection.  This, combined with the tight window of
 * valid reliable acknowledgement numbers provides protection against
 * malicious address spoofing.
 *
 * The qport field is a workaround for bad address translating routers
 * that sometimes remap the client's source port on a packet during
 * gameplay.
 *
 * If the base part of the net address matches and the qport matches,
 * then the channel matches even if the IP port differs.  The IP port
 * should be updated to the new value before sending out any replies.
 *
 * If there is no information that needs to be transfered on a given
 * frame, such as during the connection stage while waiting for the
 * client to load, then a packet only needs to be delivered if there is
 * something in the unacknowledged reliable
 */

/* optech2: bit 30 of the first sequence word marks a fragment; sequences
   never get that high (2^30 packets = 200 days at 60 Hz) */
#define NETCHAN_FRAGMENT_BIT (1U << 30)

cvar_t *showpackets;
cvar_t *showdrop;
cvar_t *net_fragmentloss; /* optech2: test only, % of fragments not sent */
cvar_t *qport;

netadr_t net_from;
sizebuf_t net_message;
byte net_message_buffer[MAX_MSGLEN];

void
Netchan_Init(void)
{
	int port;

	/* This is a little bit fishy:

	   The original code used Sys_Milliseconds() as base. It worked
	   because the original Sys_Milliseconds included some amount of
	   random data (Windows) or was dependend on seconds since epoche
	   (Unix). Our Sys_Milliseconds() always starts at 0, so there's a
	   very high propability - nearly 100 percent for something like
	   `./quake2 +connect example.com - that two or more clients end up
	   with the same qport.

	   We can't use rand() because we're always starting with the same
	   seed. So right after client start we'll nearly always get the
	   same random numbers. Again there's a high propability that two or
	   more clients end up with the same qport.

	   Just calling time() should be portable and is more less what
	   Windows did in the original code. There's still a rather small
	   propability that two clients end up with the same qport, but
	   that needs to fixed somewhere else with some kind of fallback
	   logic. */
	/* optech2: mix in the process id, so two clients started in the same
	   second on one machine (tests, LAN) still get different qports */
	port = (time(NULL) ^ (Sys_ProcessId() * 2654435761u)) & 0xffff;

	showpackets = Cvar_Get("showpackets", "0", 0);
	showdrop = Cvar_Get("showdrop", "0", 0);
	net_fragmentloss = Cvar_Get("net_fragmentloss", "0", 0);
	qport = Cvar_Get("qport", va("%i", port), CVAR_NOSET);
}

/*
 * Sends an out-of-band datagram
 */
void
Netchan_OutOfBand(int net_socket, netadr_t adr, int length, const byte *data)
{
	sizebuf_t send;
	byte send_buf[MAX_MSGLEN];

	/* write the packet header */
	SZ_Init(&send, send_buf, sizeof(send_buf));

	MSG_WriteLong(&send, -1); /* -1 sequence means out of band */
	SZ_Write(&send, data, length);

	/* send the datagram */
	NET_SendPacket(net_socket, send.cursize, send.data, adr);
}

/*
 * Sends a text message in an out-of-band datagram
 */
void
Netchan_OutOfBandPrint(int net_socket, netadr_t adr, char *format, ...)
{
	va_list argptr;
	static char string[MAX_MSGLEN - 4];

	va_start(argptr, format);
	vsnprintf(string, MAX_MSGLEN - 4, format, argptr);
	va_end(argptr);

	Netchan_OutOfBand(net_socket, adr, strlen(string), (byte *)string);
}

/*
 * called to open a channel to a remote system
 */
void
Netchan_Setup(netsrc_t sock, netchan_t *chan, netadr_t adr, int qport)
{
	memset(chan, 0, sizeof(*chan));

	chan->sock = sock;
	chan->remote_address = adr;
	chan->qport = qport;
	chan->last_received = curtime;
	chan->incoming_sequence = 0;
	chan->outgoing_sequence = 1;

	SZ_Init(&chan->message, chan->message_buf, sizeof(chan->message_buf));
	chan->message.allowoverflow = true;
}

/*
 * Returns true if the last reliable message has acked
 */
qboolean
Netchan_CanReliable(const netchan_t *chan)
{
	if (chan->reliable_length)
	{
		return false; /* waiting for ack */
	}

	return true;
}

qboolean
Netchan_NeedReliable(const netchan_t *chan)
{
	qboolean send_reliable;

	/* if the remote side dropped the last reliable message, resend it */
	send_reliable = false;

	if ((chan->incoming_acknowledged > chan->last_reliable_sequence) &&
		(chan->incoming_reliable_acknowledged != chan->reliable_sequence))
	{
		send_reliable = true;
	}

	/* if the reliable transmit buffer is empty, copy the current message out */
	if (!chan->reliable_length && chan->message.cursize)
	{
		send_reliable = true;
	}

	return send_reliable;
}

/*
 * optech2: sends one message as several packets. Each fragment repeats the
 * sequence words (bit 30 of the first marks a fragment), then a long with
 * the byte offset in bits 0-30 and "more fragments follow" in bit 31.
 */
static void
Netchan_TransmitFragments(netchan_t *chan, unsigned w1, unsigned w2,
	const byte *payload, int payload_length)
{
	sizebuf_t send;
	byte send_buf[NETCHAN_FRAGMENT_SIZE + 32];
	int offset;

	offset = 0;

	while (offset < payload_length)
	{
		int fragment_length;
		qboolean more_fragments;

		fragment_length = payload_length - offset;

		if (fragment_length > NETCHAN_FRAGMENT_SIZE)
		{
			fragment_length = NETCHAN_FRAGMENT_SIZE;
		}

		more_fragments = (offset + fragment_length < payload_length);

		SZ_Init(&send, send_buf, sizeof(send_buf));
		MSG_WriteLong(&send, w1 | NETCHAN_FRAGMENT_BIT);
		MSG_WriteLong(&send, w2);

		if (chan->sock == NS_CLIENT)
		{
			MSG_WriteShort(&send, qport->value);
		}

		MSG_WriteLong(&send, offset | (more_fragments ? (1U << 31) : 0));
		SZ_Write(&send, payload + offset, fragment_length);

		/* net_fragmentloss: pretend the network lost this fragment */
		if ((net_fragmentloss->value <= 0) ||
			((randk() % 100) >= net_fragmentloss->value))
		{
			NET_SendPacket(chan->sock, send.cursize, send.data,
				chan->remote_address);

			chan->stats_bytes_sent += send.cursize;
			chan->stats_packets_sent++;
		}

		offset += fragment_length;
	}

	chan->stats_fragmented_sent++;

	if (payload_length > chan->stats_largest_sent)
	{
		chan->stats_largest_sent = payload_length;
	}
}

/*
 * tries to send an unreliable message to a connection, and handles the
 * transmition / retransmition of the reliable messages.
 *
 * A 0 length will still generate a packet and deal with the reliable messages.
 */
void
Netchan_Transmit(netchan_t *chan, int length, const byte *data)
{
	sizebuf_t send;
	byte send_buf[MAX_MSGLEN];
	qboolean send_reliable;
	unsigned w1, w2;
	int header_length, payload_length;

	/* check for message overflow */
	if (chan->message.overflowed)
	{
		chan->fatal_error = true;
		Com_Printf("%s:Outgoing message overflow\n",
				NET_AdrToString(chan->remote_address));
		return;
	}

	send_reliable = Netchan_NeedReliable(chan);

	if (!chan->reliable_length && chan->message.cursize)
	{
		memcpy(chan->reliable_buf, chan->message_buf, chan->message.cursize);
		chan->reliable_length = chan->message.cursize;
		chan->message.cursize = 0;
		chan->reliable_sequence ^= 1;
	}

	/* write the packet header */
	SZ_Init(&send, send_buf, sizeof(send_buf));

	w1 = (chan->outgoing_sequence & ~(1U << 31)) | (send_reliable << 31);
	w2 =
		(chan->incoming_sequence &
	~(1U << 31)) | (chan->incoming_reliable_sequence << 31);

	chan->outgoing_sequence++;
	chan->last_sent = curtime;

	MSG_WriteLong(&send, w1);
	MSG_WriteLong(&send, w2);

	/* send the qport if we are a client */
	if (chan->sock == NS_CLIENT)
	{
		MSG_WriteShort(&send, qport->value);
	}

	/* copy the reliable message to the packet first */
	if (send_reliable)
	{
		SZ_Write(&send, chan->reliable_buf, chan->reliable_length);
		chan->last_reliable_sequence = chan->outgoing_sequence;
	}

	/* add the unreliable part if space is available */
	if (send.maxsize - send.cursize >= length)
	{
		SZ_Write(&send, data, length);
	}
	else
	{
		Com_Printf("%s: dumped unreliable\n", __func__);
	}

	/* optech2: split big messages into fragments (not over loopback) */
	header_length = (chan->sock == NS_CLIENT) ? 10 : 8;
	payload_length = send.cursize - header_length;

	if (chan->allow_fragments &&
		(chan->remote_address.type != NA_LOOPBACK) &&
		(payload_length > NETCHAN_FRAGMENT_SIZE))
	{
		Netchan_TransmitFragments(chan, w1, w2, send.data + header_length,
			payload_length);
	}
	else
	{
		/* send the datagram */
		NET_SendPacket(chan->sock, send.cursize, send.data, chan->remote_address);

		/* optech2: traffic counters */
		chan->stats_bytes_sent += send.cursize;
		chan->stats_packets_sent++;
	}

	if (showpackets->value)
	{
		if (send_reliable)
		{
			Com_Printf("send %4i : s=%i reliable=%i ack=%i rack=%i\n",
					send.cursize, chan->outgoing_sequence - 1,
					chan->reliable_sequence, chan->incoming_sequence,
					chan->incoming_reliable_sequence);
		}
		else
		{
			Com_Printf("send %4i : s=%i ack=%i rack=%i\n",
					send.cursize, chan->outgoing_sequence - 1,
					chan->incoming_sequence,
					chan->incoming_reliable_sequence);
		}
	}
}

/*
 * called when the current net_message is from remote_address
 * modifies net_message so that it points to the packet payload
 */
qboolean
Netchan_Process(netchan_t *chan, sizebuf_t *msg)
{
	unsigned sequence, sequence_ack;
	unsigned reliable_ack, reliable_message;
	qboolean is_fragment;
	int packet_size;

	packet_size = msg->cursize;

	/* get sequence numbers */
	MSG_BeginReading(msg);
	sequence = MSG_ReadLong(msg);
	sequence_ack = MSG_ReadLong(msg);

	/* read the qport if we are a server */
	if (chan->sock == NS_SERVER)
	{
		(void)MSG_ReadShort(msg);
	}

	reliable_message = sequence >> 31;
	reliable_ack = sequence_ack >> 31;
	is_fragment = (sequence & NETCHAN_FRAGMENT_BIT) != 0; /* optech2 */

	sequence &= ~((1U << 31) | NETCHAN_FRAGMENT_BIT);
	sequence_ack &= ~(1U << 31);

	if (showpackets->value)
	{
		if (reliable_message)
		{
			Com_Printf("recv %4i : s=%i reliable=%i ack=%i rack=%i\n",
					msg->cursize, sequence,
					chan->incoming_reliable_sequence ^ 1,
					sequence_ack, reliable_ack);
		}
		else
		{
			Com_Printf("recv %4i : s=%i ack=%i rack=%i\n",
					msg->cursize, sequence, sequence_ack,
					reliable_ack);
		}
	}

	/* discard stale or duplicated packets */
	if (sequence <= chan->incoming_sequence)
	{
		if (showdrop->value)
		{
			Com_Printf("%s:Out of order packet %i at %i\n",
					NET_AdrToString(chan->remote_address),
					sequence, chan->incoming_sequence);
		}

		return false;
	}

	/* optech2: collect fragments; the message is used once the last one
	   is in. A lost or out of order fragment loses the whole message, like
	   a lost packet (reliable data is sent again as usual). */
	if (is_fragment)
	{
		unsigned fragment_word;
		int offset, fragment_length, header_length;
		qboolean more_fragments;

		header_length = msg->readcount;
		fragment_word = MSG_ReadLong(msg);
		offset = fragment_word & ~(1U << 31);
		more_fragments = (fragment_word >> 31) != 0;
		fragment_length = msg->cursize - msg->readcount;

		chan->stats_bytes_received += packet_size;
		chan->stats_packets_received++;

		if (sequence != (unsigned)chan->fragment_sequence)
		{
			chan->fragment_sequence = sequence;
			chan->fragment_length = 0;
		}

		if ((fragment_length < 0) || (offset != chan->fragment_length) ||
			(header_length + offset + fragment_length > MAX_MSGLEN))
		{
			if (showdrop->value)
			{
				Com_Printf("%s:Dropped fragment at %i of %i\n",
					NET_AdrToString(chan->remote_address),
					offset, sequence);
			}

			chan->fragment_length = 0;
			chan->stats_fragments_dropped++;
			return false;
		}

		memcpy(chan->fragment_buf + offset, msg->data + msg->readcount,
			fragment_length);
		chan->fragment_length += fragment_length;

		if (more_fragments)
		{
			return false;
		}

		/* whole message: payload after the header this fragment had, so
		   readers (demo recording skips 8 bytes) see a normal packet */
		memcpy(msg->data + header_length, chan->fragment_buf,
			chan->fragment_length);
		msg->cursize = header_length + chan->fragment_length;
		msg->readcount = header_length;
		chan->fragment_length = 0;
		chan->stats_fragmented_received++;
	}

	/* dropped packets don't keep the message from being used */
	chan->dropped = sequence - (chan->incoming_sequence + 1);

	if (chan->dropped > 0)
	{
		if (showdrop->value)
		{
			Com_Printf("%s:Dropped %i packets at %i\n",
					NET_AdrToString(chan->remote_address),
					chan->dropped, sequence);
		}
	}

	/* if the current outgoing reliable message has been acknowledged
	 * clear the buffer to make way for the next */
	if (reliable_ack == chan->reliable_sequence)
	{
		chan->reliable_length = 0; /* it has been received */
	}

	/* if this message contains a reliable message, bump incoming_reliable_sequence */
	chan->incoming_sequence = sequence;
	chan->incoming_acknowledged = sequence_ack;
	chan->incoming_reliable_acknowledged = reliable_ack;

	if (reliable_message)
	{
		chan->incoming_reliable_sequence ^= 1;
	}

	/* the message can now be read from the current message pointer */
	chan->last_received = curtime;

	/* optech2: traffic counters (fragments were counted above) */
	if (!is_fragment)
	{
		chan->stats_bytes_received += packet_size;
		chan->stats_packets_received++;
	}

	return true;
}


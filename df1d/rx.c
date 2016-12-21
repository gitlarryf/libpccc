/*
 * This file is part of df1d.
 * Allen Bradley DF1 link layer service.
 * Copyright (C) 2007 Jason Valenzuela
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Design Systems Partners
 * Attn: Jason Valenzuela
 * 2516 JMT Industrial Drive, Suite 112
 * Apopka, FL  32703
 * jvalenzuela <at> dspfl <dot> com
 */

#include "df1.h"

#define RX_BUF_SIZE 512

static int alloc_bufs(CONN *conn);
static void cs_clear(CONN *conn);
static void cs_add(CONN *conn, uint8_t x);
static int cs_ok(const CONN *conn);
static void add_to_app(CONN *conn, uint8_t byte);
static void accept_msg(CONN *conn);
static int msg_dup(CONN *conn);
static void embed_rsp(CONN *con, uint8_t rsp);

/*
 * Description : Initializes a message receiver.
 *
 * Arguments : conn - Connection pointer.
 *             dup_detect - Duplicate message detection.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured.
 */
extern int rx_init(CONN *conn, int dup_detect)
{
  if (alloc_bufs(conn)) return -1;
  conn->rx.dup_detect = dup_detect ? 1 : 0;
  rx_set_nak(&conn->rx);
  conn->rx.state = RX_IDLE;
  /*
   * The receiver timeout is the maximum ticks allowed to elapse after
   * receiving the first application layer byte(following the DLE STX) up to
   * and including the checksum byte(s).
   */
  conn->rx.tticks = 5000000 / TICK_USEC + 1;
  log_msg(LOG_DEBUG, "%s:%d [%s] Receiver initialized with"
	  " duplicate message detection %s.\n", __FILE__, __LINE__, conn->name,
	  conn->rx.dup_detect ? "enabled" : "disabled");
  return 0;
}

/*
 * Description : Assembles raw data received from the TTY into a
 *               complete application layer message.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_msg(CONN *conn)
{
  uint8_t byte;
  if (conn->rx.state == RX_IDLE) /* Start of a new message. */
    {
      buf_empty(conn->rx.app);
      conn->rx.eticks = 0;
      conn->rx.prev_dle = 0;
      conn->rx.overflow = 0;
      cs_clear(conn);
      conn->rx.state = RX_APP;
    }
  while (!buf_get_byte(conn->tty_in, &byte))
    {
      switch (conn->rx.state)
	{
	case RX_APP:
	  add_to_app(conn, byte);
	  break;
	case RX_CS1: /* First byte of checksum. */
	  if (conn->use_crc)
	    {
	      conn->rx.msg_cs.crc = byte;
	      conn->rx.state = RX_CS2;
	    }
	  else /* Accept the message after a single byte for BCC checksums. */
	    {
	      conn->rx.msg_cs.bcc = byte;
	      accept_msg(conn);
	      return;
	    }
	  break;
	case RX_CS2: /* Second byte of CRC checksum. */
	  conn->rx.msg_cs.crc += byte << 8;
	  accept_msg(conn);
	  return;
	  break;
	  /*
	   * This should never occur, however a compiler warning results
	   * without it.
	   */
	case RX_IDLE:
	case RX_PEND:
	  break;
	}
    }
  return;
}

/*
 * Description : Places a DLE ACK in the connection's TTY output buffer.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_ack(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Sending DLE ACK.\n", __FILE__, __LINE__,
	  conn->name);
  if (buf_append_byte(conn->tty_out, SYM_DLE) ||
      buf_append_byte(conn->tty_out, SYM_ACK))
    log_msg(LOG_ERR,
	    "%s:%d [%s] Failed to send ACK due to TTY buffer full.\n",
	    __FILE__, __LINE__, conn->name);
  conn->rx.last_was_ack = 1;
  conn->rx.state = RX_IDLE;
  conn->rx.client = NULL;
  conn->dcnts.acks_out++;
  return;
}

/*
 * Description : Places a DLE NAK in the connection's TTY output buffer.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_nak(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Sending DLE NAK.\n", __FILE__, __LINE__,
	  conn->name);
  if (buf_append_byte(conn->tty_out, SYM_DLE) ||
      buf_append_byte(conn->tty_out, SYM_NAK))
    log_msg(LOG_ERR,
	    "%s:%d [%s] Failed to send NAK due to TTY buffer full.\n",
	    __FILE__, __LINE__, conn->name);
  rx_set_nak(&conn->rx);
  conn->rx.state = RX_IDLE;
  conn->rx.client = NULL;
  conn->dcnts.naks_out++;
  return;
}

/*
 * Description : Notifies the receiver that an ENQ has been received.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_enq(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Received DLE ENQ.\n", __FILE__, __LINE__,
	  conn->name);
  /*
   * If an ENQ is received while still waiting for a client to respond to
   * the received message, log an error and acknowledge the message.
   */
  if (conn->rx.state == RX_PEND)
    {
      log_msg(LOG_ERR, "%s:%d [%s.%s] Remote node transmitter timed out before"
	      " client acknowledged a received message.\n", __FILE__,
	      __LINE__, conn->name, conn->rx.client->name);
      conn->rx.client->dcnts.rx_timeouts++;
      rx_ack(conn);
      return;
    }
  if (conn->rx.last_was_ack) rx_ack(conn);
  else rx_nak(conn);
  return;
}

/*
 * Description : Receiver timeout handler. A receiver timeout occurs after a
 *               DLE STX has been received but the message is not completed
 *               within a reasonable amount of time.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_tick(CONN *conn)
{
  if ((conn->rx.state == RX_IDLE) || (conn->rx.state == RX_PEND)) return;
  if (++conn->rx.eticks > conn->rx.tticks)
    {
      log_msg(LOG_DEBUG, "%s:%d [%s] Message reception timeout.\n",
	      __FILE__, __LINE__, conn->name);
      conn->rx.state = RX_IDLE;
      rx_set_nak(&conn->rx);
    }
  return;
}

/*
 * Description : Sets a receiver's last response to NAK.
 *
 * Arguments : rx - Receiver pointer.
 *
 * Return Value : None.
 */
extern void rx_set_nak(RX *rx)
{
  rx->last_was_ack = 0;
  return;
}

/*
 * Description : Determines if a receiver is currently receiving an application
 *               layer message.
 *
 * Arguments : rx - Pointer to the receiver to query.
 *
 * Return Value : Zero if the receiver is not currently receiving a message.
 *                Non-zero if the receiver is currently receiving a message.
 */
extern int rx_active(const RX *rx)
{
  return ((rx->state == RX_IDLE) || (rx->state == RX_PEND)) ? 0 : 1;
}

/*
 * Description : Frees memory allocated for a receiver.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void rx_close(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Freeing receiver resources.\n", __FILE__,
	  __LINE__, conn->name);
  buf_free(conn->rx.app);
  return;
}

/*
 * Description : Allocates buffer space for a DF1 receiver.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero upon success.
 *                Non-zero if a memory allocation error occured.
 */
static int alloc_bufs(CONN *conn)
{
  conn->rx.app = buf_new(RX_BUF_SIZE);
  if (conn->rx.app == NULL)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error allocating receiver buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  return 0;
}

/*
 * Description : Clears a receiver's accumulated checksum.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
static void cs_clear(CONN *conn)
{
  if (conn->use_crc) conn->rx.acc_cs.crc = 0;
  else conn->rx.acc_cs.bcc = 0;
  return;
}

/*
 * Description : Adds a byte to the receiver's accumulated checksum.
 *
 * Arguments : conn - Connection pointer.
 *             x - Value to add.
 *
 * Return Value : None.
 */
static void cs_add(CONN *conn, uint8_t x)
{
  if (conn->use_crc)
    {
      register uint16_t crc = conn->rx.acc_cs.crc;
      register unsigned int i;
      CRC_ADD(i, crc, x);
      conn->rx.acc_cs.crc = crc;
    }
  else conn->rx.acc_cs.bcc += x;
  return;
}

/*
 * Description : Compares the accumulated and received checksum.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Non-zero if the accumulated checksum and received checksum
 *                match.
 *                Zero if the checksums don't match.
 */
static int cs_ok(const CONN *conn)
{
  uint16_t acc; /* Accumulated checksum. */
  uint16_t rcv; /* Received checksum. */
  if (conn->use_crc)
    {
      acc = conn->rx.acc_cs.crc;
      rcv = conn->rx.msg_cs.crc;
    }
  else
    {
      uint8_t bcc = ~conn->rx.acc_cs.bcc + 1;
      acc = bcc;
      rcv = conn->rx.msg_cs.bcc;
    }
  log_msg(LOG_DEBUG, "%s:%d [%s] Accumulated checksum - 0x%x,"
	  " received checksum - 0x%x.\n",  __FILE__, __LINE__, conn->name, acc,
	  rcv);
  return (acc == rcv) ? 1 : 0;
}

/*
 * Description : Appends a byte to the application message being received.
 *
 * Arguments : conn - Connection pointer.
 *             byte - Byte to add.
 *
 * Return Value : None.
 */
static void add_to_app(CONN *conn, uint8_t byte)
{
  switch (byte)
    {
    case SYM_ETX:
      if (conn->rx.prev_dle)
	{
	  log_msg(LOG_DEBUG, "%s:%d [%s] Received DLE ETX.\n", __FILE__,
		  __LINE__, conn->name);
	  if (conn->use_crc) /* Include the ETX when using CRC checksums. */
	    cs_add(conn, SYM_ETX);
	  conn->rx.state = RX_CS1;
	  return;
	}
      break;
    case SYM_DLE:
      if (!conn->rx.prev_dle)
	{
	  conn->rx.prev_dle = 1;
	  return;
	}
      break;
    case SYM_ACK:
    case SYM_NAK:
      if (conn->rx.prev_dle) /* Embedded responses. */
	{
	  conn->rx.prev_dle = 0;
	  embed_rsp(conn, byte);
	  return;
	}
      break;
    default: /* Anything else after a DLE is not allowed. */
      if (conn->rx.prev_dle)
	{
	  rx_set_nak(&conn->rx);
	  return;
	}
      break;
    }
  conn->rx.prev_dle = 0;
  if (!conn->rx.overflow && buf_append_byte(conn->rx.app, byte))
    {
      log_msg(LOG_DEBUG, "%s:%d [%s] Received message overflow.\n", __FILE__,
	      __LINE__, conn->name);
      conn->rx.overflow = 1;
      conn->dcnts.rx_overflow++;
    }
  cs_add(conn, byte);
  return;
}

/*
 * Description : Accepts a completed application layer message. Checks the
 *               received checksum and if the message is a duplicate. Valid
 *               messages are then passed on to the client.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
static void accept_msg(CONN *conn)
{
  if (conn->rx.app->len < 6)
    {
      log_msg(LOG_DEBUG, "%s:%d [%s] Received message is too small.\n",
	      __FILE__, __LINE__, conn->name);
      conn->dcnts.runts++;
      rx_nak(conn);
    }
  else if (cs_ok(conn))
    {
      if (msg_dup(conn)) rx_ack(conn); /* Duplicate messages are ACKed. */
      else /* ACK/NAK will be sent after client accepts or rejects. */
	{
	  conn->dcnts.msg_rx++;
	  conn->rx.state = RX_PEND;
	  client_msg_rx(conn);
	}
    }
  else /* Checksum mismatch. */ 
    {
      conn->dcnts.bad_cs++;
      rx_nak(conn);
    }
  return;
}

/*
 * Description : Tests a received message to see if it is a duplicate.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero if the message is unique or duplicate message detection
 *                is disabled.
 *                Nonzero if the message is a duplicate.
 */
static int msg_dup(CONN *conn)
{
  int dup = 0;
  if (!conn->rx.dup_detect) return 0;
  if ((conn->rx.app->data[1] == conn->rx.dup[0])
      && (conn->rx.app->data[2] == conn->rx.dup[1])
      && (conn->rx.app->data[4] == conn->rx.dup[2])
      && (conn->rx.app->data[5] == conn->rx.dup[3]))
    {
      log_msg(LOG_DEBUG, "%s:%d [%s] Received duplicate message.\n",
	      __FILE__, __LINE__, conn->name);
      conn->dcnts.dups++;
      dup = 1;
    }
  /*
   * Update duplicate message detection bytes.
   */
  conn->rx.dup[0] = conn->rx.app->data[1];
  conn->rx.dup[1] = conn->rx.app->data[2];
  conn->rx.dup[2] = conn->rx.app->data[4];
  conn->rx.dup[3] = conn->rx.app->data[5];
  return dup;
}

/*
 * Description : Handles the reception of an embedded response, a DLE ACK or
 *               a DLE NAK within an application layer packet.
 *
 * Arguments : con - Connection pointer.
 *             rsp - Embedded response received, SYM_ACK or SYM_NAK.
 *
 * Return Value : None.
 */
static void embed_rsp(CONN *con, uint8_t rsp)
{
  if (!con->embed_rsp)
    {
      con->embed_rsp = 1;
      log_msg(LOG_NOTICE, "%s:%d [%s] Detected embedded responses.\n",
	      __FILE__, __LINE__, con->name);
    }
  if (rsp == SYM_ACK) tx_ack(con);
  else tx_nak(con);
  return;
}

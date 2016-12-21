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

#define TX_BUF_SIZE 512

static int alloc_bufs(CONN *conn);
static void send_msg(CONN *conn);
static void send_enq(CONN *conn);
static void flush_msg(TX *tx);

/*
 * Description : Initializes a message transmitter.
 *
 * Arguments : conn - Connection pointer.
 *             max_nak - Maximum NAKs before giving up.
 *             max_enq - Maximum ENQs sent before giving up.
 *             ack_timeout - Milliseconds to wait for an ACK.
 *
 * Return Value : None.
 */
extern int tx_init(CONN *conn, unsigned int max_nak, unsigned int max_enq,
		   unsigned int ack_timeout)
{
  if (alloc_bufs(conn)) return -1;
  conn->tx.max_nak = max_nak;
  conn->tx.max_enq = max_enq;
  conn->tx.state = TX_IDLE;
  conn->tx.tticks = ack_timeout / (TICK_USEC / 1000);
  log_msg(LOG_DEBUG, "%s:%d [%s] Transmitter initialized."
	  " Max NAKs - %u, Mak ENQs - %u, %u tick(s) ACK timeout.\n", __FILE__,
	  __LINE__, conn->name, conn->tx.max_nak, conn->tx.max_enq,
	  conn->tx.tticks);
  return 0;
}

/*
 * Description : Accepts a message from a client for transmission over a DF1
 *               connection.
 *
 * Arguments : conn - Connection pointer.
 *             client - Originating client.
 *
 * Return Value : None.
 */
extern void tx_msg(CONN *conn, CLIENT *client)
{
  uint8_t byte;
  int overflow;
  register uint16_t crc = 0;
  register uint8_t bcc = 0;
  log_msg(LOG_DEBUG, "%s:%d [%s.%s] Beginning message transmission.\n",
	  __FILE__, __LINE__, conn->name, client->name);
  /*
   * Start the message with DLE STX.
   */
  overflow = buf_append_byte(conn->tx.msg, SYM_DLE);
  overflow |= buf_append_byte(conn->tx.msg, SYM_STX);
  /*
   * Add application layer message from the client.
   */
  while (!buf_get_byte(client->df1_tx, &byte))
    {
      overflow |= buf_append_byte(conn->tx.msg, byte);
      if (conn->use_crc)
	{
	  register unsigned int i;
	  CRC_ADD(i, crc, byte);
	}
      else bcc += byte;
      if (byte == SYM_DLE) /* Convert a DLE into a DLE DLE. */
	overflow |= buf_append_byte(conn->tx.msg, SYM_DLE);
    }
  /*
   * Close application layer message with DLE ETX.
   */
  overflow |= buf_append_byte(conn->tx.msg, SYM_DLE);
  overflow |= buf_append_byte(conn->tx.msg, SYM_ETX);
  /*
   * Append checksum.
   */
  if (conn->use_crc)
    {
      register unsigned int i;
      CRC_ADD(i, crc, SYM_ETX);
      overflow |= buf_append_word(conn->tx.msg, htols(crc));
    }
  else overflow |= buf_append_byte(conn->tx.msg, ~bcc + 1);
  conn->tx.client = client;
  if (overflow)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s.%s] Message dropped due to buffer overflow.\n",
	      __FILE__, __LINE__, conn->name, client->name);

    }
  send_msg(conn);
  conn->dcnts.tx_attempts++;
  return;
}

/*
 * Description : Notifies the transmitter that its data was completely
 *               written to the file descriptor, but not necessarily actually
 *               sent out the TTY, which may take some time depending on the
 *               current baud rate.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void tx_data_sent(CONN *conn)
{
  if (conn->tx.state == TX_PEND_MSG_TX)
    {
      conn->tx.state = TX_PEND_RESP;
      conn->tx.eticks = 0;
    }
  return;
}

/*
 * Description : Transmitter timeout handler. A transmitter timeout occurs
 *               if a message was transmitted but no ACK/NAK response was
 *               received.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void tx_tick(CONN *conn)
{
 /*
  * Pause TX timeouts while receiving.
  */
  if (!conn->embed_rsp && rx_active(&conn->rx)) return;
  if ((conn->tx.state == TX_PEND_RESP)
      && (++conn->tx.eticks > conn->tx.tticks))
    {
      log_msg(LOG_DEBUG, "%s:%d [%s] Transmitter timeout.\n", __FILE__,
	      __LINE__, conn->name);
      conn->dcnts.resp_timeouts++;
      if (++conn->tx.enq_cnt > conn->tx.max_enq) /* Too many ENQs sent. */
	{
	  log_msg(LOG_ERR, "%s:%d [%s] Message transmission failed after %u"
		  " ENQ(s) sent.\n", __FILE__, __LINE__, conn->name,
		  conn->tx.max_enq);
	  flush_msg(&conn->tx);
	  conn->dcnts.tx_fail++;
	  client_msg_tx_fail(conn);
	  return;
	}
      else send_enq(conn);
    }
  return;
}

/*
 * Description : Notifies the transmitter that an ACK symbol has been received.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void tx_ack(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Received DLE ACK.\n", __FILE__, __LINE__,
	  conn->name);
  if (conn->tx.state == TX_PEND_RESP)
    {
      flush_msg(&conn->tx);
      conn->dcnts.tx_success++;
      client_msg_tx_ok(conn);
    }
  else
    {
      log_msg(LOG_ERR, "%s:%d [%s] Received unexpected ACK.\n",
	      __FILE__, __LINE__, conn->name); 
      rx_set_nak(&conn->rx);
      conn->dcnts.bytes_ignored += 2;
    }
  return;
}

/*
 * Description : Notifies the transmitter that a NAK symbol has been received.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void tx_nak(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Received DLE NAK.\n", __FILE__, __LINE__,
	  conn->name);
  if (conn->tx.state == TX_PEND_RESP)
    {
      /*
       * Discard the message if the maximum allowable NAKs have been
       * received.
       */
      if (++conn->tx.nak_cnt >= conn->tx.max_nak)
	{
	  log_msg(LOG_ERR,
		  "%s:%d [%s] Message transmission failed after "
		  "%u NAK(s) received.\n", __FILE__, __LINE__, conn->name,
		  conn->tx.max_nak);	  
	  flush_msg(&conn->tx);
	  conn->dcnts.tx_fail++;
	  client_msg_tx_fail(conn);
	}
      else send_msg(conn); /* Retransmission. */
    }
  else
    {
      log_msg(LOG_ERR, "%s:%d [%s] Received unexpected NAK.\n",
	      __FILE__, __LINE__, conn->name); 
      rx_set_nak(&conn->rx);
      conn->dcnts.bytes_ignored += 2;
    }
  return;
}

/*
 * Description : Determines if a transmitter can accept a new message to send.
 *
 * Arguments : tx - Transmitter to query.
 *
 * Return Value : Non-zero if the transmitter is in the process of sending a
 *                message.
 *                Zero if the transmitter can accept a new message.
 */
extern int tx_busy(const TX *tx)
{
  return (tx->state == TX_IDLE) ? 0 : 1;
}

/*
 * Description : Frees memory allocated for a transmitter.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void tx_close(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Freeing transmitter resources.\n", __FILE__,
	  __LINE__, conn->name);
  buf_free(conn->tx.msg);
  return;
}

/*
 * Description : Allocates buffer space for a DF1 transmitter.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero upon success.
 *                Non-zero if a memory allocation error occured.
 */
static int alloc_bufs(CONN *conn)
{
  conn->tx.msg = buf_new(TX_BUF_SIZE);
  if (conn->tx.msg == NULL)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error allocating transmitter buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  return 0;
}

/*
 * Description : Copies a transmitter's current message to the connection's
 *               output buffer for transmission out the TTY.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
static void send_msg(CONN *conn)
{
  conn->tx.state = TX_PEND_MSG_TX;
  if (buf_append_buf(conn->tty_out, conn->tx.msg))
    {
      log_msg(LOG_ERR, "%s:%d [%s] Message transmission failed"
	      " because TTY output buffer full.\n", __FILE__, __LINE__,
	      conn->name);
      flush_msg(&conn->tx);
      client_msg_tx_fail(conn);
    }
  return;
}

/*
 * Description : Appends a DLE ENQ to a connection's TTY output buffer.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
static void send_enq(CONN *conn)
{
  conn->tx.state = TX_PEND_MSG_TX;
  log_msg(LOG_DEBUG, "%s:%d [%s] Sending DLE ENQ.\n", __FILE__, __LINE__,
	  conn->name);
  if (buf_append_byte(conn->tty_out, SYM_DLE) ||
      buf_append_byte(conn->tty_out, SYM_ENQ))
    {
      log_msg(LOG_ERR, "%s:%d [%s] ENQ transmission failed"
	      " because TTY output buffer full.\n", __FILE__, __LINE__,
	      conn->name);
      flush_msg(&conn->tx);
      client_msg_tx_fail(conn);
    }
  return;
}

/*
 * Description : Flushes the current message being transmitted and resets
 *               the retry counters.
 *
 * Arguments : tx - Transmitter pointer.
 *
 * Return Value : None.
 */
static void flush_msg(TX *tx)
{
  tx->nak_cnt = 0;
  tx->enq_cnt = 0;
  buf_empty(tx->msg);
  tx->state = TX_IDLE;
  return;
}

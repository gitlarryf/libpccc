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

#define CLIENT_BUF_SIZE 512

static int alloc_bufs(CONN *conn, CLIENT *client);
static int read_client(CONN *conn, CLIENT *client);
static int write_client(CONN *conn, CLIENT *client);
static void find_next_tx(CONN *conn, CLIENT *start_client);
static int parse_sock_data(CONN *conn, CLIENT *client);
static int rcv_app(CONN *conn, CLIENT *client, uint8_t byte);
static void rcv_ack(CONN *conn, CLIENT *client);
static void rcv_nak(CONN *conn, CLIENT *client);
static int reg_client(const CONN *conn, CLIENT *client);
static CLIENT *find_addr(const CONN *conn, uint8_t addr);
static CLIENT *close_client(CONN *conn, CLIENT *client);

/*
 * Description : Accepts new clients connecting to the listening socket.
 *               Allocates and initializes a new client structure.
 *
 * Arguments : conn - Pointer to the DF1 connection to which the new client
 *                    is connecting.
 *
 * Return Value : None.
 */
extern void client_accept(CONN *conn)
{
  CLIENT *new_client;
  CLIENT *next_client;
  int new_fd;
  int addr_len;
  struct sockaddr_in addr;
  char addr_p[INET_ADDRSTRLEN];
  addr_len = sizeof(addr);
 again:
  new_fd = accept(conn->sock_fd, (struct sockaddr *)&addr, &addr_len);
  if (new_fd < 0)
    {
      if (errno == EINTR) goto again;
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) return;
      log_msg(LOG_ERR,
	      "%s:%d [%s] Failed to accept client connection : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return;
    }
  /*
   * Allocate and initialize a new client structure.
   */
  new_client = (CLIENT *)calloc(1, sizeof(CLIENT));
  if (new_client == NULL)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error allocating memory for new client : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(new_fd);
      return;
    }
  if (alloc_bufs(conn, new_client))
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error allocating memory for client buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      free(new_client);
      close(new_fd);
      return;
    }
  new_client->fd = new_fd;
  new_client->state = CLIENT_CONNECTED;
  strncpy(new_client->name, "*!REG*", PCCC_NAME_LEN);
  /*
   * Append the new client to the connection's linked list of clients.
   */
  if (conn->clients == NULL) conn->clients = new_client;
  else
    {
      for (next_client = conn->clients; next_client->next != NULL;
	   next_client = next_client->next);
      next_client->next = new_client;
    }
  inet_ntop(AF_INET, &addr.sin_addr, addr_p, INET_ADDRSTRLEN);
  log_msg(LOG_INFO, "%s:%d [%s] Client connected from %s.\n", __FILE__,
	  __LINE__, conn->name, addr_p);
  return;
}

/*
 * Description : Loads a file descriptor set with all the descriptors for
 *               a particular connection's clients.
 *
 * Arguments : conn - Connection pointer.
 *             set - Pointer to descriptor set to populate.
 *
 * Return Value : The highest numbered file descriptor among the connection's
 *                clients.
 */
extern int client_get_read_fds(const CONN *conn, fd_set *set)
{
  CLIENT *cur;
  int high = 0;
  for (cur = conn->clients; cur != NULL; cur = cur->next)
    {
      FD_SET(cur->fd, set);
      if (cur->fd > high) high = cur->fd;
    }
  return high;
}

/*
 * Description : Searches a connection for any clients that have data waiting
 *               to be written to their respective sockets.
 *
 * Arguments : conn - Connection pointer
 *             set - File descriptor set being tested for writability.
 *
 * Return Value : Non-zero if any clients are found with data ready to be
 *                written.
 *                Zero if no clients have data needing to be written.
 */
extern int client_get_write_fds(const CONN *conn, fd_set *set)
{
  int write_pend = 0;
  CLIENT *client;
  for (client = conn->clients; client != NULL; client = client->next)
    if (buf_write_ready(client->sock_out))
      {
	FD_SET(client->fd, set);
	write_pend = 1; 
      }
  return write_pend;
}

/*
 * Description : Services the file descriptors for all the clients of a
 *               particular connection.
 *
 * Arguments : conn - Connection pointer
 *             read - File descriptors that are readable.
 *             write - File descriptors that are writable
 *             cnt - Total number of file descriptors needing service.
 *
 * Return Value : Zero if all clients were serviced without error.
 *                Non-zero if a client was closed.
 */
extern int client_service_fds(CONN *conn, const fd_set *read,
			      const fd_set *write, int *cnt)
{
  CLIENT *client;
  int ret = 0;
  for (client = conn->clients; (client != NULL) && *cnt;)
    {
      if (FD_ISSET(client->fd, read))
	{
	  *cnt--;
	  if (read_client(conn, client) || parse_sock_data(conn, client))
	    {
	      client = close_client(conn, client);
	      ret = -1;
	      continue;
	    }
	  if (!*cnt) break;
	}
      if ((write != NULL) && (FD_ISSET(client->fd, write)))
	{
	  *cnt--;
	  if (write_client(conn, client))
	    {
	      client = close_client(conn, client);
	      ret = -1;
	      continue;
	    }
	}
      client = client->next;
    }
  return ret;
}

/*
 * Description : Notifies the client that the message submitted to the
 *               transmitter has been successfully sent.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void client_msg_tx_ok(CONN *conn)
{
  if (conn->tx.client != NULL)
    {
      log_msg(LOG_DEBUG, "%s:%d [%s.%s] Sending transmission success"
	      " message to client.\n", __FILE__, __LINE__, conn->name,
	      conn->tx.client->name);
      if (buf_append_byte(conn->tx.client->sock_out, MSG_ACK))
	log_msg(LOG_ERR, "%s:%d [%s.%s] Could not send transmission"
		" success notice to client because socket buffer full.\n",
		__FILE__, __LINE__, conn->name, conn->tx.client->name);
      conn->tx.client->state = CLIENT_IDLE;
      conn->tx.client->dcnts.tx_success++;
    }
  else /* Client is defunct. */
    log_msg(LOG_ERR,
	    "%s:%d [%s] Message transmission completed for defunct client.\n",
	    __FILE__, __LINE__, conn->name);
  find_next_tx(conn, NULL);
  return;
}

/*
 * Description : Notifies the client that the transmitter failed to transmit
 *               the message.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void client_msg_tx_fail(CONN *conn)
{
  if (conn->tx.client != NULL)
    {
      log_msg(LOG_DEBUG,
	      "%s:%d [%s.%s] Sending transmission failure message.\n",
	      __FILE__, __LINE__, conn->name, conn->tx.client->name);
      if (buf_append_byte(conn->tx.client->sock_out, MSG_NAK))
	log_msg(LOG_ERR, "%s:%d [%s.%s] Could not send transmission"
		" failure notice to client because socket buffer full.\n",
		__FILE__, __LINE__, conn->name, conn->tx.client->name);
      conn->tx.client->state = CLIENT_IDLE;
      conn->tx.client->dcnts.tx_fail++;
    }
  else /* Client is defunct. */
    log_msg(LOG_ERR,
	    "%s:%d [%s] Message transmission failed for defunct client.\n",
	    __FILE__, __LINE__, conn->name);
  find_next_tx(conn, NULL);
  return;
}

/*
 * Description : Accepts a message from the DF1 receiver and passes it on
 *               to the appropriate client.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
extern void client_msg_rx(CONN *conn)
{
  uint8_t dst; /* Message's destination address. */
  buf_get_byte(conn->rx.app, &dst);
  CLIENT *client = find_addr(conn, dst);
  if (client == NULL) /* No client found with matching address. */
    {
      log_msg(LOG_ERR, "%s:%d [%s] Message received for unknown "
	      "destination address - %u.\n", __FILE__, __LINE__,
	      conn->name, dst);
      conn->dcnts.unknown_dst++;
      rx_ack(conn);
    }
  else
    {
      size_t new_len;
      log_msg(LOG_DEBUG,
	      "%s:%d [%s.%s] Sending received message to client.\n",
	      __FILE__, __LINE__, conn->name, client->name);
      new_len = client->sock_out->len + conn->rx.app->len + 2;
      if (new_len > client->sock_out->max)
	{
	  log_msg(LOG_ERR, "%s:%d [%s.%s] Received message dropped"
		  " because client's socket buffer full.\n", __FILE__,
		  __LINE__, conn->name, client->name);
	  rx_nak(conn);
	  client->dcnts.sink_full++;
	}
      buf_append_byte(client->sock_out, MSG_SOH);
      buf_append_byte(client->sock_out, conn->rx.app->len);
      buf_append_buf(client->sock_out, conn->rx.app);
      conn->rx.client = client;
      client->dcnts.msg_rx++;
    }
  return;
}

/*
 * Description : Closes all the clients of a particular connection.
 *
 * Arguments : conn - Target connection.
 *
 * Return Value : None
 */
extern void client_close_all(CONN *conn)
{
  CLIENT *client;
  for (client = conn->clients; client != NULL;
       client = close_client(conn, client));
  return;
}

/*
 * Description : Allocates buffer space for a new client.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero upon success.
 *                Non-zero if a memory allocation error occured.
 */
static int alloc_bufs(CONN *conn, CLIENT *client)
{
  client->df1_tx = buf_new(CLIENT_BUF_SIZE);
  if (client->df1_tx == NULL) return -1;
  client->sock_out = buf_new(CLIENT_BUF_SIZE);
  if (client->sock_out == NULL)
    {
      buf_free(client->df1_tx);
      return -1;
    }
  client->sock_in = buf_new(CLIENT_BUF_SIZE);
  if (client->sock_in == NULL)
    {
      buf_free(client->df1_tx);
      buf_free(client->sock_out);
      return -1;
    }
  return 0;
}

/*
 * Description : Reads data from a client's socket.
 *
 * Arguments : conn - Connection pointer.
 *             client - Client to read from.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured with read() or the client closed
 *                the connection.
 */
static int read_client(CONN *conn, CLIENT *client)
{
  ssize_t len = buf_read(client->fd, client->sock_in);
  if (len < 0)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s.%s] Failed to read from client's socket : %s\n",
	      __FILE__, __LINE__, conn->name, client->name, strerror(errno));
      return -1;
    }
  else if (!len) /* Client closed connection. */
    {
      log_msg(LOG_INFO, "%s:%d [%s.%s] Client disconnected.\n", __FILE__,
	      __LINE__, conn->name, client->name);
      return -1;
    }
  log_msg(LOG_DEBUG, "%s:%d [%s.%s] Received %u bytes from client.\n",
	  __FILE__, __LINE__, conn->name, client->name, client->sock_in->len);
  return 0;
}

/*
 * Description : Writes data to a client's socket.
 *
 * Arguments : conn - Connection pointer.
 *             client - Target client.
 *
 * Return Value : Zero upon success.
 *                Non-zero if write() failed.
 */
static int write_client(CONN *conn, CLIENT *client)
{
  ssize_t len = buf_write(client->fd, client->sock_out);
  if (len < 0)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s.%s] Failed to write to client's socket : %s\n",
	      __FILE__, __LINE__, conn->name, client->name, strerror(errno));
      return -1;
    }
  log_msg(LOG_DEBUG, "%s:%d [%s.%s] Wrote %u byte(s) to client.\n", __FILE__,
	  __LINE__, conn->name, client->name, len);
  return 0;
}

/*
 * Description : Searches a connection's clients to find the next message
 *               needing to be transmitted over the DF1 link.
 *
 * Arguments : conn - Connection pointer.
 *             start_client - Client to start searching at. NULL to start at
 *                            the client following that most recently serviced
 *                            by the transmitter.
 *
 * Return Value : None.
 */
static void find_next_tx(CONN *conn, CLIENT *start_client)
{
  CLIENT *client;
  if (tx_busy(&conn->tx)) return; /* Transmitter currently in use. */
  if (conn->clients == NULL) return; /* No more clients. */
  if (start_client == NULL)
    {
      if (conn->tx.client == NULL) start_client = conn->clients;
      else
	{
	  start_client = conn->tx.client->next;
	  if (start_client == NULL) start_client = conn->clients;
	}
    }
  client = start_client;
  do
    {
      if (client->state == CLIENT_MSG_READY)
	{
	  tx_msg(conn, client);
	  buf_empty(client->df1_tx);
	  client->state = CLIENT_MSG_PEND;
	  client->dcnts.tx_attempts++;
	  break;
	}
      client = client->next;
      if (client == NULL) client = conn->clients;
    } while (client != start_client);
  return;
}

/*
 * Description : Parses data received from a client.
 *
 * Arguments : conn - Connection pointer.
 *             client - Source client.
 *
 * Return Value : Non-zero if an error occured.
 */
static int parse_sock_data(CONN *conn, CLIENT *client)
{
  uint8_t byte;
  while (!buf_get_byte(client->sock_in, &byte))
    {
      switch (client->state)
	{
	case CLIENT_CONNECTED: /* First byte received is the client address. */
	  client->addr = byte;
	  client->state = CLIENT_REG_LEN;
	  break;
	case CLIENT_REG_LEN: /* Second byte should be the name length. */
	  client->name_len = byte;
	  client->state = CLIENT_REG_NAME;
	  break;
	case CLIENT_REG_NAME:
	  client->name[client->name_len_rcvd++] = byte;
	  if (client->name_len_rcvd == client->name_len)
	    {
	      client->name[client->name_len] = 0;
	      if (reg_client(conn, client)) return -1;
	    }
	  break;
	case CLIENT_IDLE:
	  if (byte == MSG_SOH)
	    {
	      log_msg(LOG_DEBUG, "%s:%d [%s.%s] Receiving new"
		      " application layer message from client.\n", __FILE__,
		      __LINE__, conn->name, client->name);
	      client->state = CLIENT_MSG_LEN;
	      break;
	    }
	  /* Intentional fall-through. */
	case CLIENT_MSG_READY:
	case CLIENT_MSG_PEND:
	  switch (byte)
	    {
	    case MSG_SOH: /* Only one outstanding message allowed at a time. */
	      log_msg(LOG_ERR, "%s:%d [%s.%s] Message received from client "
		      "while one is already pending transmission.\n", __FILE__,
		      __LINE__, conn->name, client->name);
	      return -1;
	      break;
	    case MSG_ACK: /* Client acknowledged a received message. */
	      rcv_ack(conn, client);
	      break;
	    case MSG_NAK: /* Client rejected a received message. */
	      rcv_nak(conn, client);
	      break;
	    default:
	      log_msg(LOG_ERR, "%s:%d [%s.%s] Received unknown message type "
		      "from client.\n", __FILE__, __LINE__, conn->name,
		      client->name);
	      return -1;
	      break;
	    }	  
	  break;
	case CLIENT_MSG_LEN:
	  client->new_msg_len = byte;
	  client->state = CLIENT_MSG;
	  break;
	case CLIENT_MSG:
	  if (rcv_app(conn, client, byte)) return -1;
	  break;
	}
    }
  return 0;
}

/*
 * Description : Assembles an incoming application layer message from a client
 *               in that client's df1_tx buffer.
 *
 * Arguments : conn - Connection pointer.
 *             client - Pointer to the client sourcing the message.
 *             byte - The next byte from the application layer message.
 *
 * Return Value : Zero if successfull.
 *                Non-zero if the application message being received
 *                overflowed the client's application message buffer.
 */
static int rcv_app(CONN *conn, CLIENT *client, uint8_t byte)
{
  if (buf_append_byte(client->df1_tx, byte))
    {
      log_msg(LOG_ERR, "%s:%d [%s.%s] Buffer overflow while receiving "
	      "application data.", __FILE__, __LINE__, conn->name,
	      client->name);
      return -1;
    }
  /*
   * Once the message is completely received, queue it for transmission.
   */
  if (client->df1_tx->len == client->new_msg_len)
    {
      client->state = CLIENT_MSG_READY;
      find_next_tx(conn, client);
    }
  return 0;
}

/*
 * Description :
 *
 * Arguments :
 *
 * Return Value : None.
 */
static void rcv_ack(CONN *conn, CLIENT *client)
{
  if (conn->rx.client == client)
    {
      log_msg(LOG_DEBUG,
	      "%s:%d [%s.%s] Client accepted message from receiver.\n",
	      __FILE__, __LINE__, conn->name, client->name);
      rx_ack(conn);
      client->dcnts.msg_accept++;
    }
  else
    log_msg(LOG_ERR, "%s:%d [%s.%s] Received unexpected ACK from client.\n",
	    __FILE__, __LINE__, conn->name, client->name);
  return;
}

/*
 * Description :
 *
 * Arguments :
 *
 * Return Value : None.
 */
static void rcv_nak(CONN *conn, CLIENT *client)
{
  if (conn->rx.client == client)
    {
      log_msg(LOG_DEBUG,
	      "%s:%d [%s.%s] Client rejected message from receiver.\n",
	      __FILE__, __LINE__, conn->name, client->name);
      rx_nak(conn);
      client->dcnts.msg_reject++;
    }
  else
    log_msg(LOG_ERR, "%s:%d [%s.%s] Received unexpected NAK from client.\n",
	    __FILE__, __LINE__, conn->name, client->name);
  return;
}

/*
 * Description : Handles the registration of a new client.
 *
 * Arguments : conn - Connection accepting the registration.
 *             new_client - Client registering.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the client tried to register at an address that
 *                is already in use.
 */
static int reg_client(const CONN *conn, CLIENT *new_client)
{
  CLIENT *client = find_addr(conn, new_client->addr);
  if (client != NULL)
    {
      log_msg(LOG_ERR, "%s:%d [%s.%s] Client tried to register at address %u"
	      " which is already used by client %s.\n", __FILE__, __LINE__,
	      conn->name, new_client->name, new_client->addr, client->name);
      return -1;
    }
  log_msg(LOG_INFO, "%s:%d [%s.%s] Client registered at address %u.\n",
	  __FILE__, __LINE__, conn->name, new_client->name, new_client->addr);
  new_client->state = CLIENT_IDLE;
  return 0;
}

/*
 * Description : Finds a registered client with a matching address.
 *
 * Arguments : conn - Connection to search.
 *             addr - Desired address.
 *
 * Return Value : A pointer to the matching client.
 *                NULL if no matching client was found.
 */
static CLIENT *find_addr(const CONN *conn, uint8_t addr)
{
  CLIENT *client;
  for (client = conn->clients; client != NULL; client = client->next)
    if ((client->state >= CLIENT_IDLE) && (client->addr == addr)) break;
  return client;
}

/*
 * Description : Frees memory allocated for a client and closes it's
 *               socket file descriptor.
 *
 * Arguments : conn - Pointer to the DF1 connection associated with the
 *                    target client.
 *             client - Pointer to the target client.
 *
 * Return Value : A pointer to the next client in the connection's linked
 *                list. NULL of the client was at the end of the list.
 */
static CLIENT *close_client(CONN *conn, CLIENT *client)
{
  CLIENT *next_client = client->next;
  log_msg(LOG_INFO, "%s:%d [%s.%s] Closing client.\n", __FILE__,
	  __LINE__, conn->name, client->name);
  log_msg(LOG_INFO, "%s:%d [%s.%s] Client stats: %u msgs tx; %u msgs rx.\n",
	  __FILE__, __LINE__, conn->name, client->name,
	  client->dcnts.tx_attempts, client->dcnts.msg_rx);
  /*
   * If the client being closed currently has a message out for transmission,
   * set the transmitter's client pointer to NULL so that it doesn't try to
   * notify a defunct client of the result of the transmission.
   */
  if (conn->tx.client == client) conn->tx.client = NULL;
  /*
   * If the client was sent a received message, but has not yet acknowledged
   * it, send an ACK.
   */
  if (conn->rx.client == client) rx_ack(conn);
  /*
   * Remove the client from the connection's linked list.
   */
  if (conn->clients == client) conn->clients = client->next;
  else
    {
      CLIENT *prev_client;
      for (prev_client = conn->clients; prev_client->next != client;
	   prev_client = prev_client->next);
      prev_client->next = client->next;
    }
  if (close(client->fd))
    log_msg(LOG_ERR,
	    "%s:%d [%s.%s] Error closing client file descriptor : %s\n",
	    client->name, __FILE__, __LINE__, strerror(errno));
  buf_free(client->df1_tx);
  buf_free(client->sock_out);
  buf_free(client->sock_in);
  free(client);
  return next_client;
}

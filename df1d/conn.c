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

#define LISTEN_BACKLOG 5

static int sock_init(CONN *conn, in_port_t port);
static void parse_tty_data(CONN *conn);
static CONN *close_conn(CONN *target);

static CONN *head; /* Pointer to the top of the linked list of connections. */

/*
 * Description : Allocates and initializes a new connection instance.
 *
 * Arguments : name - Text name for connection.
 *             tty_dev - Serial port device.
 *             tty_rate - Serial port baud rate.
 *             use_crc - Non-zero to use CRC checksums, BCC otherwise.
 *             sock_port - TCP port to bind to for client connections.
 *             tx_max_nak - Max NAKs allowed before transmission failure.
 *             tx_max_enq - Max ENQs allowed before transmission failure.
 *             rx_dup_detect - Non-zero to enable receiver duplicate message
 *                             detection.
 *
 * Return Value : None.
 */
extern void conn_init(const char *name, const char *tty_dev, int tty_rate,
		      int use_crc, in_port_t sock_port,
		      unsigned int tx_max_nak, unsigned int tx_max_enq,
		      int rx_dup_detect, unsigned int ack_timeout)
{
  CONN *new;
  log_msg(LOG_INFO, "%s:%d [%s] Initializing connection.\n", __FILE__,
	  __LINE__, name);
  new = (CONN *)calloc(1, sizeof(CONN));
  if (new == NULL)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Failed to allocate memory for new"
	      " connection : %s\n", __FILE__, __LINE__, name,
	      strerror(errno));
      return;
    }
  strncpy(new->name, name, CONN_NAME_LEN);
  new->use_crc = use_crc;
  if (tty_open(new, tty_dev, tty_rate))
    {
      free(new);
      return;
    }
  if (sock_init(new, sock_port))
    {
      tty_close(new);
      free(new);
      return;
    }
  if (tx_init(new, tx_max_nak, tx_max_enq, ack_timeout))
    {

      tty_close(new);
      free(new);
      return;
    }
  if (rx_init(new, rx_dup_detect))
    {


      tx_close(new);
      tty_close(new);
      free(new);
      return;
    }
  /*
   * Append the new connection to the linked list.
   */
  if (head == NULL) head = new;
  else
    {
      CONN *end;
      for (end = head; end->next != NULL; end = end->next);
      end->next = new;
    }
  return;
}

/*
 * Description : Assembles all active file descriptors for all connections.
 *
 * Arguments : set - File descriptor set to add active descriptors to.
 *
 * Return Value : One more than the highest numbered file descriptor in use.
 *                Zero if no file descriptors are active.
 */
extern int conn_get_read_fds(fd_set *set)
{
  CONN *cur;
  int high = 0; /* Highest numbered descriptor. */
  FD_ZERO(set);
  for (cur = head; cur != NULL; cur = cur->next)
    {
      int high_client;
      FD_SET(cur->tty_fd, set);
      if (cur->tty_fd > high) high = cur->tty_fd;
      FD_SET(cur->sock_fd, set);
      if (cur->sock_fd > high) high = cur->sock_fd;
      high_client = client_get_read_fds(cur, set);
      if (high_client > high) high = high_client;
    }
  return high ? high + 1 : 0;
}

/*
 * Description : Checks all connection buffers to see if any data is waiting
 *               to be transmitted either out a TTY or to a client's socket.
 *               If data is pending, the associated descriptor is added to
 *               the descriptor set.
 *
 * Arguments : set - File descriptor set to add descriptors to be tested.
 *
 * Return Value : Zero if no descriptors need to be tested.
 *                Non-zero if data is pending to be written on any descriptor.
 */
extern int conn_get_write_fds(fd_set *set)
{
  CONN *cur = head;
  int write_pend = 0;
  FD_ZERO(set);
  do
    {
      if (buf_write_ready(cur->tty_out))
	{
	  FD_SET(cur->tty_fd, set);
	  write_pend = 1;
	}
      write_pend |= client_get_write_fds(cur, set);
      cur = cur->next;
    } while (cur != NULL);
  return write_pend;
}

/*
 * Description : Services readable/writable file descriptors for all
 *               connections.
 *
 * Arguments : read - File descriptors that are ready for reading.
 *             write - File descriptors that are writable. NULL if none.
 *             cnt - Total number of file descriptors that are ready.
 *
 * Return Value : Zero if all descriptors were serviced successfully.
 *                Non-zero if the a connection was closed or a client was
 *                added or removed.
 */
extern int conn_service_fds(const fd_set *read, const fd_set *write, int *cnt)
{
  int ret = 0;
  CONN *cur = head;
  do
    {
      if (client_service_fds(cur, read, write, cnt)) ret = 1;
      if (FD_ISSET(cur->tty_fd, read))
	{
	  if (tty_read(cur))
	    {
	      cur = close_conn(cur);
	      ret = -1;
	      continue;
	    }
	  parse_tty_data(cur);
	  if (!--*cnt) return ret;
	}
      if ((write != NULL) && FD_ISSET(cur->tty_fd, write))
	{
	  if (tty_write(cur))
	    {
	      cur = close_conn(cur);
	      ret = -1;
	      continue;
	    }
	  if (!--*cnt) return ret;
	}
      if (FD_ISSET(cur->sock_fd, read)) /* New client connecting. */
	{
	  client_accept(cur);
	  ret = 1;
	  *cnt--;
	}
      cur = cur->next;
    } while (cur != NULL);
  return ret;
}

/*
 * Description : Connection timeout handler.
 *
 * Arguments : None.
 *
 * Return Value : None.
 */
extern void conn_tick(void)
{
  CONN *cur = head;
  do
    {
      rx_tick(cur);
      tx_tick(cur);
      cur = cur->next;
    } while (cur != NULL);
  return;
}

/*
 * Description : Closes all connections.
 *
 * Arguments : None.
 *
 * Return Value : None.
 */
extern void conn_close_all(void)
{
  while (head != NULL) close_conn(head);
  return;
}

/*
 * Description : Creates a listening TCP socket for client connections.
 *
 * Arguments : conn - Pointer to the parent connection.
 *             port - TCP port number to bind to.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured.
 */
static int sock_init(CONN *conn, in_port_t port)
{
  int flags = 1;
  struct sockaddr_in addr;
  conn->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (conn->sock_fd < 0)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Listening socket creation failed : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  if (setsockopt(conn->sock_fd, SOL_SOCKET, SO_REUSEADDR, &flags,
		 sizeof(int)))
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Failed to set listening socket options : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  /*
   * Set the socket to non-blocking.
   */
  flags = fcntl(conn->sock_fd, F_GETFL, 0);
  if (flags < 0)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error retrieveing socket status flags : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(conn->sock_fd);
      return -1;
    }
  if (fcntl(conn->sock_fd, F_SETFL, flags | O_NONBLOCK))
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Failed to set socket status flags : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(conn->sock_fd);      
      return -1;
    }
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Bind to all interfaces. */
  if (bind(conn->sock_fd, (struct sockaddr *)&addr, sizeof(addr)))
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error binding listening socket : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(conn->sock_fd);
      return -1;
    }
  if (listen(conn->sock_fd, LISTEN_BACKLOG))
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error setting listening socket to listen : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(conn->sock_fd);
      return -1;
    }
  return 0;
}

/*
 * Description : Parses the raw data received from a TTY looking for
 *               link layer symbols. Application layer bytes are passed on to
 *               the message receiver.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : None.
 */
static void parse_tty_data(CONN *conn)
{
  int more = 1; /* Non-zero while bytes from the TTY remain to be parsed. */
  while (more)
    {
      uint8_t byte;
      if (rx_active(&conn->rx)) rx_msg(conn);
      more = !buf_get_byte(conn->tty_in, &byte);
      if (more)
	{
	  if (conn->read_sym) /* Previous link layer byte was a DLE. */
	    {
	      conn->read_sym = 0;
	      /*
	       * ETX and embedded reponses are detected by the receiver,
	       * not here.
	       */
	      switch (byte)
		{
		case SYM_STX:
		  log_msg(LOG_DEBUG, "%s:%d [%s] Received DLE STX.\n",
			  __FILE__, __LINE__, conn->name);
		  rx_msg(conn);
		  continue;  
		  break;
		case SYM_ENQ:
		  conn->dcnts.enqs_in++;
		  rx_enq(conn);
		  continue;
		  break;
		case SYM_ACK:
		  conn->dcnts.acks_in++;
		  tx_ack(conn);
		  continue;
		  break;
		case SYM_NAK:
		  conn->dcnts.naks_in++;
		  tx_nak(conn);
		  continue;
		  break;
		default: /* Any other character after a DLE is invalid. */
		  log_msg(LOG_DEBUG, "%s:%d [%s] Spurious byte received.\n",
			  __FILE__, __LINE__, conn->name);
		  conn->dcnts.bytes_ignored++;
		  rx_set_nak(&conn->rx);
		  break;
		}
	    }
	  if (byte == SYM_DLE) conn->read_sym = 1;
	  else /* Any link data not prefixed with a DLE is ignored. */
	    {
	      log_msg(LOG_DEBUG, "%s:%d [%s] Spurious byte received.\n",
		      __FILE__, __LINE__, conn->name);
	      conn->dcnts.bytes_ignored++;
	      rx_set_nak(&conn->rx);
	      conn->read_sym = 0;
	    }
	}
    }
  return;
}

/*
 * Description : Closes a connection and all of it's associated clients.
 *
 * Arguments : target - Pointer to connection to close.
 *
 * Return Value : A pointer to the next connection in the linked list of
 *                connections. NULL if the target was the last in the list.
 */
static CONN *close_conn(CONN *target)
{
  CONN *next = target->next;
  log_msg(LOG_INFO, "%s:%d [%s] Closing connection.\n", __FILE__, __LINE__,
	  target->name);
  log_msg(LOG_INFO, "%s:%d [%s] Connection stats: %u msgs tx; %u msgs rx.\n",
	  __FILE__, __LINE__, target->name, target->dcnts.tx_attempts,
	  target->dcnts.msg_rx);
  /*
   * Remove the connection from the linked list of connections.
   */
  if (head == target) head = target->next;
  else
    {
      CONN *prev;
      for (prev = head; prev->next != target; prev = prev->next);
      prev->next = target->next;
    }
  client_close_all(target);
  rx_close(target);
  tx_close(target);
  tty_close(target);
  if (close(target->sock_fd))
    log_msg(LOG_ERR, "%s:%d [%s] Error closing listening socket : %s\n",
	    __FILE__, __LINE__, target->name, strerror(errno));
  free(target);
  return next;
}

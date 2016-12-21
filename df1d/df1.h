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

#ifndef _DF1_H
#define _DF1_H

/*
 * Definitions for required for pselect() on some systems.
 */
#define _GNU_SOURCE
#define _XOPEN_SOURCE 601

#include <errno.h>
#include <fcntl.h>
//#include <iconv.h>
#include <limits.h>
//#include <pwd.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "../common.h"
#include "../lib/pccc.h"

#ifdef _WIN32
#include <winsock.h>
#define LOG_DEBUG   1
#define LOG_ERR     2
#define LOG_INFO    4
#define LOG_NOTICE  8
#define O_FLAGS O_RDWR
#define PATH_MAX MAX_PATH
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <libxml/parser.h>
#include <libxml/valid.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#define O_FLAGS O_RDWR | O_NOCTTY | O_NONBLOCK
#endif


/*
 * Embedded symbol definitions.
 */
#define SYM_STX 0x02
#define SYM_ETX 0x03
#define SYM_ENQ 0x05
#define SYM_ACK 0x06
#define SYM_NAK 0x15
#define SYM_DLE 0x10

/*
 * System tick period.
 */
#define TICK_SEC 0
#define TICK_USEC 10000

#define CONN_NAME_LEN 16 /* Maximum length of connection name. */

#define CRC_ADD(i, crc, val)  \
for (crc ^= val, i = 8; i--;) \
  if (crc & 1)                \
    {                         \
      crc >>= 1;              \
      crc ^= 0xa001;          \
    }                         \
  else crc >>= 1

struct link_diag_cnt /* Per-connection diagnostic counters. */
{
  unsigned int tx_attempts; /* Messages attempted to send. */
  unsigned int tx_success; /* Messages successfully sent. */
  unsigned int msg_rx; /* Messages successfully received. */
  unsigned int acks_in; /* ACKs received. */
  unsigned int naks_in; /* NAKs received. */
  unsigned int resp_timeouts; /* Timeouts awaiting a response. */
  unsigned int enqs_out; /* ENQs sent. */
  unsigned int tx_fail; /* Messages that could not be sent. */
  unsigned int acks_out; /* ACKs sent. */
  unsigned int naks_out; /* NAKs sent. */
  unsigned int enqs_in; /* ENQs received. */
  unsigned int runts; /* Messages too small. */
  unsigned int bad_cs; /* Received bad checksums. */
  unsigned int unknown_dst; /* Destination node not found. */
  unsigned int bytes_ignored; /* Spurious bytes received. */
  unsigned int dups; /* Duplicate messages received. */
  unsigned int rx_overflow; /* Receiver overflows. */
};

struct client_diag_cnt /* Per-client diagnostic counters. */
{
  unsigned int tx_attempts; /* Messages transmission attempts.*/
  unsigned int tx_success; /* Messages successfully transmitted. */
  unsigned int tx_fail; /* Messages failed to transmit. */
  unsigned int sink_full; /* Messages rejected because socket buffer full. */
  unsigned int msg_rx; /* Messages received destined for client. */
  unsigned int msg_reject; /* Messages received but rejected by client. */
  unsigned int msg_accept; /* Messages received and accepted by client. */
  unsigned int rx_timeouts; /* Timed out awaiting response from client. */
};

typedef enum /* Client states. */
  {
    CLIENT_CONNECTED, /* Connection accepted, pending registration. */
    CLIENT_REG_LEN, /* Next byte should be the length of the client's name. */
    CLIENT_REG_NAME, /* Receiving client name. */
    CLIENT_IDLE, /* Client registered, ready for messages. */
    CLIENT_MSG_LEN, /* Next byte is length of application layer message. */
    CLIENT_MSG, /* Receiving application layer message. */
    CLIENT_MSG_READY, /* Application layer message completely received. */
    CLIENT_MSG_PEND /* Application layer message submitted to transmitter. */
  } CLIENT_STATE_T;

typedef struct _client /* Client specific data. */
{
  char name[PCCC_NAME_LEN + 1];
  int fd; /* Socket file descriptor. */
  CLIENT_STATE_T state;
  uint8_t addr; /* Source node address. */
  uint8_t name_len; /* Length of the client's name. */
  uint8_t name_len_rcvd; /* Number of name characters received. */
  uint8_t new_msg_len; /* Size of application layer message from client. */
  BUF *df1_tx; /* Message to be transmitted on behalf of the client. */
  BUF *sock_out; /* Data to be transmitted to the client. */
  BUF *sock_in; /* Data received from the client. */
  struct client_diag_cnt dcnts;
  struct _client *next; /* Next client in the linked list. */
} CLIENT;

typedef enum /* Transmitter states. */
  {
    TX_IDLE, /* No message pending transmission. */
    TX_PEND_MSG_TX, /* Message in output buffer, pending write(). */
    TX_PEND_RESP /* Data completely written to fd, pending response. */
  } TX_STATE_T;

typedef struct _tx /* Message transmitter data. */
{
  TX_STATE_T state;
  unsigned int max_nak; /* Number of NAK's received before giving up. */
  unsigned int max_enq; /* Number of ENQ's solicited before giving up. */
  unsigned int nak_cnt; /* Number of successive NAK's received. */
  unsigned int enq_cnt; /* Number of ENQ's transmitted. */
  unsigned int eticks; /* Elapsed timer ticks. */
  unsigned int tticks; /* Number of ticks before a timeout occurs. */
  BUF *msg; /* Current message being transmitted. */
  CLIENT *client; /* Client who's message is currently being transmitted. */
} TX;

typedef enum /* Receiver states. */
  {
    RX_IDLE,
    RX_APP, /* DLE STX received, parsing application message bytes. */
    RX_CS1, /* DLE ETX received, reading first byte of checksum. */
    RX_CS2, /* Reading second checksum byte, applies to CRC only. */
    RX_PEND /* Received message ok, pending client acceptance. */
  } RX_STATE_T;

typedef struct _rx /* Message receiver data. */
{
  RX_STATE_T state;
  BUF *app; /* Received application data. */ 
  uint8_t dup[4]; /* Bytes used to detect a duplicate packet. */
  unsigned int eticks;  /* Elapsed timer ticks. */
  unsigned int tticks; /* Number of ticks before a timeout occurs. */
  unsigned last_was_ack : 1; /* Set if the last response sent was an ACK. */
  unsigned overflow : 1; /* Set if message being received has overflowed. */
  unsigned dup_detect : 1; /* Set if duplicate message detection enabled. */
  unsigned prev_dle : 1; /* Set if the previous application byte was DLE. */
  CLIENT *client; /* Pointer to the client which received the message. */
  union /* Checksum received from message. */
  {
    uint16_t crc;
    uint8_t bcc;
  } msg_cs;
  union /* Accumulated checksum. */
  {
    uint16_t crc;
    uint8_t bcc;
  } acc_cs;
} RX;

typedef enum
  {
    DUPLEX_FULL,
    DUPLEX_MASTER, /* Half duplex master. */
    DUPLEX_SLAVE /* Half duplex slave. */ 
  } DUPLEX_T;

typedef struct _conn /* DF1 connection instance data. */
{
  char name[CONN_NAME_LEN + 1];
  int tty_fd; /* Serial port file descriptor. */
  int sock_fd; /* Socket listening for new client connections. */
  DUPLEX_T duplex; /* Duplex mode. */
  unsigned use_crc : 1; /* Set if using CRC checksums, BCC otherwise. */
  unsigned read_sym : 1; /* Set if the previous link layer byte was a DLE. */
  unsigned embed_rsp : 1; /* Set if embedded responses were detected. */
  long byte_usec; /* Time in uS to transmit one byte at current baud rate. */
  BUF *tty_in; /* Raw data received from the TTY. */
  BUF *tty_out; /* Data to be transmitted out the TTY. */
  TX tx; /* Transmitter data. */
  RX rx; /* Receiver data. */
  CLIENT *clients; /* Linked list of clients. */
  struct link_diag_cnt dcnts;
  struct _conn *next; /* Pointer to the next connection. */
} CONN;

extern int cfg_read(const char *file);

extern void conn_init(const char *name, const char *tty_dev, int tty_rate,
		      int use_crc, in_port_t sock_port,
		      unsigned int tx_max_nak, unsigned int tx_max_enq,
		      int rx_dup_detect, unsigned int ack_timeout);
extern int conn_get_read_fds(fd_set *set);
extern int conn_get_write_fds(fd_set *set);
extern int conn_service_fds(const fd_set *read, const fd_set *write, int *cnt);
extern void conn_tick(void);
extern void conn_close_all(void);

extern void client_accept(CONN *conn);
extern int client_get_read_fds(const CONN *conn, fd_set *set);
extern int client_get_write_fds(const CONN *conn, fd_set *set);
extern int client_service_fds(CONN *conn, const fd_set *read,
			      const fd_set *write, int *cnt);
extern void client_msg_tx_ok(CONN *conn);
extern void client_msg_tx_fail(CONN *conn);
extern void client_msg_rx(CONN *conn);
extern void client_close_all(CONN *conn);

extern int tty_open(CONN *conn, const char *dev, int rate);
extern int tty_read(CONN *conn);
extern int tty_write(CONN *conn);
extern void tty_close(CONN *conn);

extern int tx_init(CONN *conn, unsigned int max_nak, unsigned int max_enq,
		   unsigned int ack_timeout);
extern void tx_msg(CONN *conn, CLIENT *client);
extern void tx_data_sent(CONN *conn);
extern void tx_tick(CONN *conn);
extern void tx_ack(CONN *conn);
extern void tx_nak(CONN *conn);
extern int tx_busy(const TX *tx);
extern void tx_close(CONN *conn);

extern int rx_init(CONN *conn, int dup_detect);
extern void rx_msg(CONN *conn);
extern void rx_ack(CONN *conn);
extern void rx_nak(CONN *conn);
extern void rx_enq(CONN *conn);
extern void rx_tick(CONN *conn);
extern void rx_set_nak(RX *rx);
extern int rx_active(const RX *rx);
extern void rx_close(CONN *conn);

extern void log_open(int log_to_console, int lev);
extern void log_msg(int lev, const char *fmt, ...);
extern void log_close(void);

extern int timer_start(void);
extern int timer_stop(void);

#endif /* _DF1_H */

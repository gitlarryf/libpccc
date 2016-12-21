/*
 * This file is part of libpccc.
 * Allen Bradley PCCC message library.
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

#ifndef _PRIVATE_H
#define _PRIVATE_H

//#include "../common.h"
#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define BUF_SIZE 300 /* Size of internal message buffers. */

#define MSG_UNUSED 0
#define MSG_PEND 1 /* Pending transmission to link layer. */
#define MSG_TX 2 /* Pending acknowledgement from link layer. */
#define MSG_ACK_RCVD 4 /* Received acknowledgment from link layer. */
#define MSG_REPLY_RCVD 8 /* Received reply from remote node. */
#define MSG_CMD_DONE (MSG_TX | MSG_ACK_RCVD | MSG_REPLY_RCVD)

#define PCCC_ERR_LEN 256

typedef enum
  {
    READ_MODE_IDLE,
    READ_MODE_MSG_LEN,
    READ_MODE_MSG
  } READ_MODE_T;

/*
 * This structure holds a message to be sent over the link and
 * its associated data.
 */
typedef struct _msg
{
  /*
   * These elements are valid for both command and reply messages.
   */
  int state;
  BUF *buf; /* Actual message data sent over the link. */
  unsigned is_cmd : 1; /* Set if a command, zero if a reply. */
  /*
   * The following elements are only used for command messages.
   */
  uint16_t tns; /* Transaction number. */
  PCCC_FT_T file_type; /* Type of data read/written. */
  size_t bytes; /* Number of bytes read/written. */
  size_t elements; /* Number of elements transferred. */
  size_t usize; /* Host size of the element type being transferred. */
  time_t expires; /* Time at which waiting for a reply times out. */
  void *udata; /* User data being read/written.  */
  UFUNC notify; /* User notification function when command is complete. */
  int (* reply)(BUF *, struct _msg *, char *); /* Pointer to reply handler. */
  PCCC_RET_T result;
  char errstr[PCCC_ERR_LEN];
} DF1MSG;

/*
 * Private connection data not exposed in the public connection structure.
 */
typedef struct _pccc_priv
{
  uint16_t tns; /* Next tranaction number to be used. */
  BUF *sock_in; /* Bytes to be transmitted to link layer. */
  BUF *sock_out; /* Bytes received from link layer. */
  BUF *msg_in; /* Assembled message being received from link layer. */
  READ_MODE_T read_mode;
  uint8_t msg_in_len; /* Size of message being received from link layer. */
  size_t num_msgs;
  DF1MSG *cur_msg; /* Pointer to current message being transmitted. */
  DF1MSG *msgs; /* */
  unsigned connected : 1; /* Set if connected to link layer. */
  char errstr[PCCC_ERR_LEN]; /* Additional error description. */
} PCCC_PRIV;

/*
 * Pointer to a function that will parse a reply from a command initiated
 * locally.
 */
typedef int (* RFUNC)(BUF *, DF1MSG *, char *);

extern int msg_init(PCCC_PRIV *p);
extern DF1MSG *msg_get_free(PCCC_PRIV *p);
extern PCCC_RET_T msg_send(PCCC_PRIV *p);
extern int msg_is_reply(const PCCC_PRIV *p);
extern DF1MSG *msg_find_cmd(PCCC_PRIV *p);
extern PCCC_RET_T msg_send_next(PCCC_PRIV *p);
extern size_t msg_get_len(const BUF *msg);
extern uint8_t msg_get_src(const BUF *msg);
extern uint8_t msg_get_cmd(const BUF *msg);
extern uint16_t msg_get_tns(const BUF *msg);
extern uint8_t msg_get_sts(const BUF *msg);
extern uint8_t msg_get_ext_sts(const BUF *msg);
extern int msg_get_owner_node(const BUF *msg, uint8_t *on);
extern void msg_abort_all(PCCC *con);
extern void msg_flush(DF1MSG *m);
extern void msg_free(PCCC_PRIV *p);

extern PCCC_RET_T cmd_init(PCCC *con, DF1MSG **pm, UFUNC notify, RFUNC reply,
			   uint8_t dnode, void *udata, uint8_t cmd,
			   uint8_t func);
extern PCCC_RET_T cmd_send(PCCC *con, DF1MSG *cmd);

/*
 * Reply handlers. All reply handlers must conform to the same prototype.
 */
extern int reply_Echo(BUF *rply, DF1MSG *cmd, char *err);
extern int reply_ProtectedTypedLogicalRead(BUF *rply, DF1MSG *cmd, char *err);
extern int reply_ReadSLCFileInfo(BUF *rply, DF1MSG *cmd, char *err);
extern int reply_ReadLinkParam(BUF *rply, DF1MSG *cmd, char *err);
extern int reply_Dummy(BUF *rply, DF1MSG *cmd, char *err);

extern int sts_check(PCCC *con, const BUF *msg);

extern int addr_encode(BUF *dest, uint16_t addr);
extern int addr_decode(BUF *src, uint16_t *addr);
extern PCCC_RET_T addr_enc_plc(BUF *dst, const PCCC_PLC_ADDR *src, char *err);

extern PCCC_RET_T data_enc_array(DF1MSG *msg, char *err);
extern PCCC_RET_T data_dec_array(BUF *rply, DF1MSG *msg, char *err);
extern PCCC_RET_T data_enc_td(BUF *dst, uint64_t type, uint64_t size, char *err);
extern int data_dec_td(BUF *src, uint64_t *type, uint64_t *size, char *err);

#endif /* _PRIVATE_H */

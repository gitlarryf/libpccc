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

#include "pccc.h"
#include "private.h"

/*
* Description : Initializes an array of message buffers for a
*               connection.
*
* Arguments :
*
* Return Value : Zero if successful.
*                Non-zero if a memory allocation error occured.
*/
extern int msg_init(PCCC_PRIV *p)
{
    unsigned int i;
    p->msgs = (DF1MSG *)malloc(sizeof(DF1MSG) * p->num_msgs);
    if (p->msgs == NULL) return -1;
    for (i = 0; i < p->num_msgs; i++) {
        p->msgs[i].buf = buf_new(BUF_SIZE);
        if (p->msgs[i].buf == NULL) {
            while (--i >= 0) buf_free(p->msgs[i].buf);
            return -1;
        }
        msg_flush(p->msgs + i);
    }
    return 0;
}

/*
* Description : Finds an available message buffer.
*
* Arguments : p -
*
* Return Value : Pointer to the next available message.
*                NULL if no message buffers are available.
*/
extern DF1MSG *msg_get_free(PCCC_PRIV *p)
{
    register unsigned int i;
    for (i = 0; i < p->num_msgs; i++)
        if (p->msgs[i].state == MSG_UNUSED) {
            p->msgs[i].state = MSG_PEND;
            return p->msgs + i;
        }
    return NULL;
}

/*
* Description : Copies the current message to the socket transmission buffer
*               prefixing a SOH and length byte.
*
* Arguments : p -
*
* Return Value : PCCC_SUCCESS if no errors occured.
*                PCCC_EOVERFLOW if the socket output buffer would overflow.
*/
extern PCCC_RET_T msg_send(PCCC_PRIV *p)
{
    if (buf_append_byte(p->sock_out, MSG_SOH) ||
        buf_append_byte(p->sock_out, p->cur_msg->buf->len) ||
        buf_append_buf(p->sock_out, p->cur_msg->buf)) {
        strncpy(p->errstr, "msg_send()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    p->cur_msg->state = MSG_TX;
    return PCCC_SUCCESS;
}

/*
* Description : Checks to see of the message received from the link layer is
*               a command or reply. Reply messages have bit six set in their
*               CMD byte.
*
* Arguments : p - Pointer to a connection's private data in which to test.
*
* Return Value : Zero if the received message is a command.
*                Non-zero if the received message is a reply.
*/
extern int msg_is_reply(const PCCC_PRIV *p)
{
    return (p->msg_in->data[2] & 0x40) ? 1 : 0;
}

/*
* Description : Finds a command that matches the received reply.
*
* Arguments : p -
*
* Return Value : Pointer to the matching command message.
*                NULL if no matching command message was found.
*/
extern DF1MSG *msg_find_cmd(PCCC_PRIV *p)
{
    DF1MSG *msg = NULL;
    DF1MSG *test;
    int i;
    uint16_t tns = msg_get_tns(p->msg_in);
    for (i = 0; i < p->num_msgs; i++) {
        test = p->msgs + i;
        if ((test->state != MSG_UNUSED) && test->is_cmd && (test->tns == tns)) {
            msg = p->msgs + i;
            break;
        }
    }
    return msg;
}

/*
* Description : Finds the next message to transmit to the link layer.
*
* Arguments : p -
*
* Return Value :
*/
extern PCCC_RET_T msg_send_next(PCCC_PRIV *p)
{
    register int i;
    DF1MSG *last = p->msgs + (p->num_msgs - 1);
    /*
     * Do nothing if a message is already being transmitted.
     */
    if (p->cur_msg->state == MSG_TX) return PCCC_SUCCESS;
    if (p->cur_msg == last) p->cur_msg = p->msgs;
    else p->cur_msg++;
    for (i = p->num_msgs; i; i--) {
        if (p->cur_msg->state == MSG_PEND) {
            PCCC_RET_T ret;
            ret = msg_send(p);
            if (ret != PCCC_SUCCESS) return ret;
            break;
        }
        if (p->cur_msg == last) p->cur_msg = p->msgs;
        else p->cur_msg++;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Gets the size of the application data in a message.
*
* Arguments : msg - Pointer to the message to query.
*
* Return Value : The size in bytes of the message's application data.
*/
extern size_t msg_get_len(const BUF *msg)
{
    return msg->len - 6;
}

/*
* Description : Retrieves a message's source node address.
*
* Arguments : msg - Pointer to message to query.
*
* Return Value : The message's source node address.
*/
extern uint8_t msg_get_src(const BUF *msg)
{
    return msg->data[1];
}

/*
* Description : Retrieves a message's command value.
*
* Arguments : msg - Pointer to message to query.
*
* Return Value : The message's command value.
*/
extern uint8_t msg_get_cmd(const BUF *msg)
{
    return msg->data[2] & 0x0f;
}

/*
* Description : Retrieves the transaction number from the message received
*               from the link layer.
*
* Arguments : msg - Pointer to source message.
*
* Return Value : The message's transaction number.
*/
extern uint16_t msg_get_tns(const BUF *msg)
{
    uint16_t *tns = (uint16_t *)(&msg->data[4]);
    return ltohs(*tns);
}

/*
* Description : Retrieves a message's status code.
*
* Arguments : msg - Pointer to message to query.
*
* Return Value : The message's status code value.
*/
extern uint8_t msg_get_sts(const BUF *msg)
{
    return msg->data[3];
}

/*
* Description : Retrieves a message's extended status code.
*
* Arguments : msg - Pointer to message to query.
*
* Return Value : The message's extended status code value.
*/
extern uint8_t msg_get_ext_sts(const BUF *msg)
{
    return msg->data[6];
}

/*
* Description : Retieves the owner node for some extended status values.
*
* Arguments : msg - Pointer to message to query.
*             on - Pointer to location to store the node value.
*
* Return Value : Zero if the owner node value was not present.
*                Non-zero if the owner node value was found.
*/
extern int msg_get_owner_node(const BUF *msg, uint8_t *on)
{
    if (msg->len < 6) return 0;
    *on = msg->data[5];
    return 1;
}

/*
* Description : Aborts all outstanding messages and issues a callback
*               indicating no connection to the link layer.
*
* Arguments : con - Connection pointer.
*
* Return Value : None.
*/
extern void msg_abort_all(PCCC *con)
{
    register int i;
    PCCC_PRIV *p = (PCCC_PRIV *)con->priv_data;
    for (i = 0; i < p->num_msgs; i++) {
        if (p->msgs[i].state != MSG_UNUSED) {
            if (p->msgs[i].notify != NULL) {
                strncpy(p->errstr, "Connection closed", PCCC_ERR_LEN);
                p->msgs[i].notify(con, PCCC_ELINK, p->msgs[i].udata);
            }
            msg_flush(p->msgs + i);
        }
    }
    return;
}

/*
* Description : Clears a message buffer and marks it as unused.
*
* Arguments : m - Pointer to target message.
*
* Return Value : None.
*/
extern void msg_flush(DF1MSG *m)
{
    m->state = MSG_UNUSED;
    m->expires = 0;
    buf_empty(m->buf);
    return;
}

/*
* Description : Frees the allocated message buffers.
*
* Arguments :
*
* Return Value : None.
*/
extern void msg_free(PCCC_PRIV *p)
{
    unsigned int i;
    for (i = 0; i < p->num_msgs; buf_free(p->msgs[i++].buf));
    free(p->msgs);
    return;
}

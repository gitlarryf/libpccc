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

/**
\page cmd_init Sending PCCC commands

These functions send actual PCCC commands. pccc_new() and pccc_connect() must be called successfully before these can be used. They can operate in two
different modes, non-blocking or one-at-a-time. In non-blocking operation they will return immediately, unless one or more parameters were invalid. The user
application must test the link layer file descriptor, call the associated
pccc_read/write functions and the library will issue a callback with the
outcome. In one-at-a-time mode, the function will block until the command
has completed and the outcome will be expressed in the return value.

The first three parameters to each function are the same:
- con - A pointer the the link layer connection.
- notify - A pointer to a user defined function that the library will call
when the command is complete. To specify one-at-a-time operation, use a value
of NULL.
- dnode - The destination node address for the command. In point to point, full
duplex connections, the value given here doesn't matter much as far as I can
tell. However if you are sending commands to a node on a different network via
some type of interface module, this needs to be set to the address of the
target node on the remote network. For example if you are using a DF1
connection to a 1747-KE to send commands to nodes on a DH-485 network,
this would be the DH-485 address of the target node.

If a user notification function is specified, using the notify argument,
it must follow this prototype:

void (PCCC *, PCCC_RET_T, void *)

The arguments to this function are as follows:

- A pointer to the \link pccc connection \endlink that initiated the command.
- One of the following \ref PCCC_RET_T "PCCC_RET_T" enumerations describing
the outcome of the command.
- A void pointer to the user data supplied to the command. Typically
this returned value is only used for command functions that read data from a
node.

All command functions may return one of these \ref PCCC_RET_T "PCCC_RET_T"
enumerations. Some functions may return additional error codes. If so, they are
listed in the individual function's documentation.
- PCCC_SUCCESS
- For non-blocking operation, success is defined as all supplied parameters
were accepted and the command has been queued for transmission.
- For one-at-a-time operation, success is defined as the command has been
sent, the reply has been received and parsed successfully.
- PCCC_ENOCON if called with a NULL connection pointer.
- PCCC_ENOBUF if no message buffers were available.
- PCCC_ECMD_NODELIVER if the link layer service could not deliver the command.
- PCCC_ECMD_TIMEOUT if a timeout occured while awaiting a reply.
- PCCC_ECMD_REPLY if the received reply message contained an error.
- PCCC_ELINK if a problem occured with the connection to the link layer
service.
- PCCC_EOVERFLOW if an internal buffer overflow occured while assembling
the command.
- PCCC_EFATAL if a fatal error occured.

Available commands organized by function:

- Reading from data tables
- pccc_cmd_ProtectedTypedLogicalRead2AddressFields()
- pccc_cmd_ProtectedTypedLogicalRead3AddressFields()

- Writing to data tables
- pccc_cmd_ProtectedTypedLogicalWrite2AddressFields()
- pccc_cmd_ProtectedTypedLogicalWrite3AddressFields()
- pccc_cmd_ProtectedTypedLogicalWriteWithMask()
- pccc_cmd_ReadModifyWrite()
- pccc_cmd_BitWrite()

- Changing processor modes
- pccc_cmd_ChangeModeMicroLogix1000()
- pccc_cmd_ChangeModeSLC500()
- pccc_cmd_SetCPUMode()

- Misc
- pccc_cmd_Echo()
- pccc_cmd_ReadSLCFileInfo()
- pccc_cmd_DisableForces()
- pccc_cmd_SetVariables()
- pccc_cmd_SetTimeout()
- pccc_cmd_SetNAKs()
- pccc_cmd_SetENQs()
- pccc_cmd_ReadLinkParam()
- pccc_cmd_SetLinkParam()
*/

#include "pccc.h"
#include "private.h"

static PCCC_RET_T send_oaat(PCCC *con, DF1MSG *cmd);
static PCCC_RET_T oaat_timeout(PCCC *con, DF1MSG *cmd);

/*
* Description : Finds and initializes a new command message. This function
*               is used by all pccc_cmd functions to acquire a message buffer
*               in which to assemble a command message.
*
* Arguments : con - Link layer connection pointer.
*             pm - Pointer to the message pointer being used in the calling
*                  command function.
*             notify - Pointer to a uyser notification function.
*             reply - Reply parser function.
*             dnode - Destination node.
*             cmd - PCCC command code.
*             func - PCCC function code. Ignored for certian CMD values.
*
* Return Value : PCCC_SUCCESS if no errors occured.
*                PCCC_ELINK if the connection pointer points to a connection
*                           that is not connected to a link layer service.
*                PCCC_ECMD_NOBUF if no message buffers are available.
*                PCCC_EOVERLFOW if a buffer overflow occured.
*/
extern PCCC_RET_T cmd_init(PCCC *con, DF1MSG **pm, UFUNC notify, RFUNC reply,
                           uint8_t dnode, void *udata, uint8_t cmd,
                           uint8_t func)
{
    DF1MSG *msg = *pm;
    int overflow = 0;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) {
        strncpy(con_priv->errstr, "Not connected", PCCC_ERR_LEN);
        return PCCC_ELINK;
    }
    msg = msg_get_free(con_priv);
    if (msg == NULL) return PCCC_ECMD_NOBUF;
    msg->is_cmd = 1;
    msg->udata = udata;
    msg->notify = notify;
    msg->reply = reply;
    msg->tns = con_priv->tns++;
    if (buf_append_byte(msg->buf, dnode)
        || buf_append_byte(msg->buf, con->src_addr)
        || buf_append_byte(msg->buf, cmd)
        || buf_append_byte(msg->buf, 0) /* STS byte. */
        || buf_append_word(msg->buf, htols(msg->tns)))
        overflow = 1;
    switch (cmd) {
        case 0x00: /* These commands have no FNC byte. */
        case 0x01:
        case 0x02:
        case 0x04:
        case 0x05:
        case 0x08:
            break;
        default:
            if (buf_append_byte(msg->buf, func)) overflow = 1;
            break;
    }
    if (overflow) {
        strncpy(con_priv->errstr, "cmd_init()", PCCC_ERR_LEN);
        msg_flush(msg);
        return PCCC_EOVERFLOW;
    }
    *pm = msg; /* Assign the new message buffer back to the calling command. */
    return PCCC_SUCCESS;
}

/*
* Description : After a pccc_cmd function has successfully assembled
*               a command message, this function is called to transmit
*               the message to the link layer.
*
* Arguments : con - Link layer connection pointer.
*             cmd - Command to send.
*
* Return Value : PCCC_SUCCESS if the command was sent and a reply
*                             was successfully received and parsed.
*                PCCC_ECMD_NODELIVER if the link layer service was
*                                    unable to transmit the command.
*                PCCC_ECMD_REPLY if the received reply contained an error.
*                PCCC_ECMD_TIMEOUT if a timeout occured awaiting a reply.
*                PCCC_EFATAL if a fatal error occured.
*/
extern PCCC_RET_T cmd_send(PCCC *con, DF1MSG *cmd)
{
    if (cmd->notify == NULL) /* NULL notification means one-at-a-time. */
        return send_oaat(con, cmd);
    return msg_send_next((PCCC_PRIV *)con->priv_data);
}

/*
* Description : This function sends a command in a 'one at a time' fashion.
*               It will send the command and block until a reply is received
*               or an error occurs.
*
* Arguments : con - Link layer connection pointer.
*             cmd - Command to send.
*
* Return Value : PCCC_SUCCESS if the command was sent and a reply
*                             was successfully received and parsed.
*                PCCC_ECMD_NODELIVER if the link layer service was
*                                    unable to transmit the command.
*                PCCC_ECMD_REPLY if the received reply contained an error.
*                PCCC_ECMD_TIMEOUT if a timeout occured awaiting a reply.
*                PCCC_EFATAL if a fatal error occured.
*/
static PCCC_RET_T send_oaat(PCCC *con, DF1MSG *cmd)
{
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    /*
    * Transmit the command to the link layer.
    */
    con_priv->cur_msg = cmd;
    ret = msg_send(con_priv);
    if (ret != PCCC_SUCCESS) return ret;
    ret = pccc_write(con);
    if (ret != PCCC_SUCCESS) return ret;
    /*
    * Wait for the ACK and the reply.
    */
    for (;;) {
        int ack_sent = 0;
        ret = oaat_timeout(con, cmd);
        if (ret != PCCC_SUCCESS) return ret;
        ret = pccc_read(con);
        if (ret != PCCC_SUCCESS) return ret;
        /*
        * If the command has been marked as unused, the link layer responded
        * to the command with a NAK, meaning it was unable to deliver
        * the message.
        */
        if (cmd->state == MSG_UNUSED) return PCCC_ECMD_NODELIVER;
        /*
        * Send an ACK to the link layer after the reply has been received.
        * The ACK byte has already been placed in the output buffer by
        * parse_msg().
        */
        if ((cmd->state & MSG_REPLY_RCVD) && !ack_sent) {
            ack_sent = 1;
            ret = pccc_write(con);
            if (ret != PCCC_SUCCESS) return ret;
            break;
        }
        if (cmd->state == MSG_CMD_DONE) break;
    }
    msg_flush(cmd);
    con_priv->msg_in->index = 6; /* Set buffer index to first byte of data. */
    /*
    * Check the returned STS and parse the reply.
    */
    return
        sts_check(con, con_priv->msg_in)
        || (cmd->reply && cmd->reply(con_priv->msg_in, cmd, con_priv->errstr))
        ? PCCC_ECMD_REPLY : PCCC_SUCCESS;
}

/*
* Description : Uses select() to block waiting for a reply while imposing
*               a timeout. This function is only used in one-at-a-time
*               operation.
*
* Arguments : con - Link layer connection pointer.
*             cmd - Pointer to the command current command being sent.
*
* Return Value : PCCC_SUCCESS when it is ok to read() from the link layer
*                             socket.
*                PCCC_ECMD_TIMEOUT if a timeout occured after receiving an
*                                  acknowledgment of the original command
*                                  message but still awaiting a reply.
*                PCCC_EFATAL if the select() system call fails.
*/
static PCCC_RET_T oaat_timeout(PCCC *con, DF1MSG *cmd)
{
    int num_fds;
    struct timeval timeout;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    fd_set read_test;
    /*
    * Do not block with timeout if the initial command message has not yet been
    * acknowledged by the link layer.
    */
    if (!(cmd->state & MSG_ACK_RCVD)) return PCCC_SUCCESS;
    /*
    * Do not block with timeout if a message, which *SHOULD* be the awaited
    * reply, is currently being received from the link layer.
    */
    if (con_priv->read_mode != READ_MODE_IDLE) return PCCC_SUCCESS;
    FD_ZERO(&read_test);
    FD_SET(con->fd, &read_test);
    timeout.tv_sec = con->timeout;
    timeout.tv_usec = 0;
    num_fds = select(con->fd + 1, &read_test, NULL, NULL, &timeout);
    if (num_fds < 0) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "select() failed : %s", strerror(errno));
        return PCCC_EFATAL;
    }
    if (!num_fds) /* Timed out */
    {
        msg_flush(cmd);
        return PCCC_ECMD_TIMEOUT;
    }
    return PCCC_SUCCESS;
}

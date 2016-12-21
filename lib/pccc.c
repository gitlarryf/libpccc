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

/** \file pccc.c */

/**
\page conn_mgmt Connection management functions

These functions deal with the setup, operation, and tear down of link
layer connections. Before any commands can be handled, a link layer connection
must be created, connected to and registered with a link layer service.

- pccc_new() - Allocates and initializes a new link layer connection.
- pccc_connect() - Connects to and registers with a link layer service.
- pccc_read() - Reads data from the link layer TCP socket.
- pccc_write_ready() - Tests to see if data is pending transmission to the
link layer connection.
- pccc_write() - Writes data to the link layer TCP socket.
- pccc_tick() - Checks for timed out commands.
- pccc_close() - Closes the connection to the link layer service.
- pccc_free() - Frees memory allocated for a connection.
- pccc_errstr() - Generates a string describing an error.
*/

#include "pccc.h"
#include "private.h"

static int alloc_bufs(PCCC_PRIV *p);
static void free_bufs(PCCC_PRIV *p);
static int get_addr(struct sockaddr_in *addr, const char *hostname, in_port_t port);
static PCCC_RET_T send_reg(PCCC *con, const char *name);
static PCCC_RET_T parse_link(PCCC *con);
static void parse_msg(PCCC *con);
static int rcv_ack(PCCC *con);
static void rcv_nak(PCCC *con);

/**
Allocates and initializes a new PCCC connection. This must be called before
any other function. The pointer returned is then used to reference a specific
connection with all other functions.

\param src_addr This is the source node address that will be used when
sending messages. It is also the address that is registered with the link
layer service so that any messages sent to this address will be routed by the
link layer to this connection. It must be unique among all clients of a
particular link layer connection.
\param timeout Number of seconds to wait for a reply to a command. The timeout
begins after the initial command message as been delivered. Must be non-zero.
\param msgs Number of outstanding message buffers to allocate.
When using non-blocking operation, this is the total of all outstanding messages.
Keep in mind that any incoming commands require a message buffer for a
reply message. For one-at-a-time operation, only one is required.
Must be non-zero.

\return
- A pointer to a new \link pccc PCCC structure \endlink if successful.
- NULL if a memory allocation error occured or one of the parameters was
invalid.
*/
extern PCCC *pccc_new(uint8_t src_addr, unsigned int timeout, size_t msgs)
{
    PCCC *con;
    PCCC_PRIV *con_priv;
    if (!timeout || !msgs) return NULL;
    con = (PCCC *)calloc(1, sizeof(PCCC));
    if (con == NULL) return NULL;
    con_priv = (PCCC_PRIV *)calloc(1, sizeof(PCCC_PRIV));
    if (con_priv == NULL) {
        free(con);
        return NULL;
    }
    if (alloc_bufs(con_priv)) {
        free(con);
        free(con_priv);
        return NULL;
    }
    con_priv->num_msgs = msgs;
    if (msg_init(con_priv)) {
        free(con);
        free(con_priv);
        free_bufs(con_priv);
        return NULL;
    }
    con_priv->cur_msg = con_priv->msgs;
    con->src_addr = src_addr;
    con->timeout = timeout;
    srandom(time(NULL));
    con_priv->tns = random(); /* Randomize the starting transaction number. */
    con_priv->tns ^= getpid();
    if (!con_priv->tns) con_priv->tns = 42; /* Don't start at zero. */
    con_priv->read_mode = READ_MODE_IDLE;
    con->priv_data = (void *)con_priv;
    return con;
}

/**
After a successfull call to pccc_new(), this must be called to actually
establish the connection to the link layer service. If the registration
fails, the link layer service will simply close the TCP connection, which
will not create an error until the next read/write operation. Once connected
and registered, commands may now be sent and any incoming commands sent to
the link layer service will be routed to this connection.

\param con Pointer to the link layer connection.
\param link_host Pointer to a string containing the hostname or IP address
of the link layer service.
\param link_port TCP port number of the link layer service.
\param client_name Pointer to string containing a name that will be used
to register with the link layer service. Log messages from the link layer
service specific to this client will contain this name. This string must be
no longer than \ref PCCC_NAME_LEN "PCCC_NAME_LEN" long.

\return
- PCCC_SUCCESS if the connection was established and the registration message was sent
successfully.
- PCCC_ELINK if an error occured with the connection to the link layer service.
- PCCC_EPARAM if the hostname or client name was invalid.
- PCCC_EFATAL if a fatal error occured.
*/
extern PCCC_RET_T pccc_connect(PCCC *con, const char *link_host, in_port_t link_port, const char *client_name)
{
    PCCC_PRIV *con_priv;
    struct sockaddr_in addr;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (con_priv->connected) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Already connected");
        return PCCC_ELINK;
    }
    if (get_addr(&addr, link_host, link_port)) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Could not resolve hostname %s : %s", link_host, hstrerror(h_errno));
        return PCCC_EPARAM;
    }
    con->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (con->fd < 0) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "socket() failed : %s", strerror(errno));
        return PCCC_EFATAL;
    }
again:
    if (connect(con->fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
        if (errno == EINTR) goto again;
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Failed to connect : %s", strerror(errno));
        return PCCC_ELINK;
    }
    con_priv->connected = 1;
    ret = send_reg(con, client_name);
    if (ret != PCCC_SUCCESS) return ret;
    return PCCC_SUCCESS;
}

/**
Reads data from the link layer connection. This function only needs to be
called by the user application when using non-blocking operation. During
non-blocking operation, this function will handle any incoming messages and
generate appropriate callbacks. When using one-at-a-time commands, it is
called internally from the command initiation functions.

\param con Pointer to the link layer connection.

\return
- PCCC_SUCCESS if successful.
- PCCC_ENOCON if the supplied connection pointer was NULL.
- PCCC_ELINK if an error occured with the connection to the link layer service.
- PCCC_EFATAL if a fatal error occured.
*/
extern PCCC_RET_T pccc_read(PCCC *con)
{
    PCCC_PRIV *con_priv;
    ssize_t len;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) {
        strncpy(con_priv->errstr, "Not connected", PCCC_ERR_LEN);
        return PCCC_ELINK;
    }
    len = buf_read(con->fd, con_priv->sock_in);
    if (len < 0)
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Error reading : %s", strerror(errno));
    else if (!len)
        strncpy(con_priv->errstr, "Remote end closed connection", PCCC_ERR_LEN);
    else 
        return parse_link(con);
    return PCCC_ELINK;
}

/**
Tests to see if data is pending transmission to the link layer connection.
When using non-blocking operation, use this function to determine whether to
test the connection's file descriptor for writeability.

\param con Pointer to the link layer connection.

\return
- PCCC_SUCCESS if no data is pending.
- PCCC_WREADY if data is pending.
- PCCC_ELINK if not currently connected to a link layer service.
- PCCC_ENOCON if the supplied connection pointer was NULL.
*/
extern PCCC_RET_T pccc_write_ready(const PCCC *con)
{
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) {
        strncpy(con_priv->errstr, "Not connected", PCCC_ERR_LEN);
        return PCCC_ELINK;
    }
    return buf_write_ready(con_priv->sock_out) ? PCCC_WREADY : PCCC_SUCCESS;
}

/**
Writes data to the link layer socket. This function only needs to be called by
the user application when using non-blocking operation. This is typically called
after pccc_write_ready() has returned PCCC_WREADY and the connection's file descriptor
is writable.

When using one-at-a-time commands, this function is called internally by the command initiation
functions.

\param con Pointer to the link layer connection.

\return
- PCCC_SUCCESS if no errors occured.
- PCCC_ENOCON if the supplied connection pointer was NULL.
- PCCC_ELINK if the connection to the link layer service was interrupted.
*/
extern PCCC_RET_T pccc_write(PCCC *con)
{
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) {
        strncpy(con_priv->errstr, "Not connected", PCCC_ERR_LEN);
        return PCCC_ELINK;
    } else if (buf_write(con->fd, con_priv->sock_out) < 0) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Error writing : %s", strerror(errno));
        return PCCC_ELINK;
    }
    return PCCC_SUCCESS;
}

/**
Checks to see if any outstanding commands have timed out awaiting a reply.
This function is only used in non-blocking operation. It should be called at
least once every second. Any command that has timed out will have its
notification function called from here.

If signals are being used to provide timing, do not call this directly from
a signal handler. Instead set a flag and call it from the mainline execution.

\param con Pointer to the link layer connection.

\return
- PCCC_SUCCESS if no errors occured.
- PCCC_ENOCON if the supplied connection pointer was NULL.
- PCCC_EFATAL if a fatal error occured.
*/
extern PCCC_RET_T pccc_tick(PCCC *con)
{
    PCCC_PRIV *con_priv;
    int i;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) return PCCC_SUCCESS;
    for (i = 0; i < con_priv->num_msgs; i++) {
        DF1MSG *msg = con_priv->msgs + i;
        /*
         * Only check messages that are commands and have been acknowledged by
         * the link layer.
         */
        if (msg->is_cmd && msg->expires) {
            time_t now = time(NULL);
            if (now == -1) {
                snprintf(con_priv->errstr, PCCC_ERR_LEN, "time() failed : %s", strerror(errno));
                return PCCC_EFATAL;
            }
            /*
             * If a message has expired, mark it as unused and notify the user
             * application.
             */
            if (now >= msg->expires) {
                msg_flush(msg);
                if (msg->notify != NULL)
                    msg->notify(con, PCCC_ECMD_TIMEOUT, msg->udata);
            }
        }
    }
    return PCCC_SUCCESS;
}

/**
Closes the connection to the link layer service. If using non-blocking
commands, any outstanding commands will have their callback functions
called with PCCC_ELINK as the second argument.

\param con Pointer to the link layer connection to close.

\return
- PCCC_SUCCESS if the connection was successfully closed.
- PCCC_ENOCON if the supplied connection pointer was NULL.
- PCCC_EFATAL if an error occured closing the TCP socket.
*/
extern PCCC_RET_T pccc_close(PCCC *con)
{
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (!con_priv->connected) return PCCC_SUCCESS;
    con_priv->connected = 0;
    msg_abort_all(con);
    buf_empty(con_priv->sock_in);
    buf_empty(con_priv->sock_out);
    buf_empty(con_priv->msg_in);
    con_priv->read_mode = READ_MODE_IDLE;
    con_priv->cur_msg = con_priv->msgs;
again:
    if (close(con->fd)) {
        if (errno == EINTR) goto again;
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "close() failed : %s", strerror(errno));
        return PCCC_EFATAL;
    }
    return PCCC_SUCCESS;
}

/**
Frees memory allocated for a connection. This should be called after
pccc_close() has been called and the connection will no longer be used.

\param con Pointer to connection to free.

\return Does not return a value.
*/
extern void pccc_free(PCCC *con)
{
    PCCC_PRIV *con_priv;
    if (con == NULL) return;
    con_priv = (PCCC_PRIV *)con->priv_data;
    free_bufs(con_priv);
    msg_free(con_priv);
    free(con->priv_data);
    free(con);
    return;
}

/**
Generates a string describing an error value returned from a PCCC function.
The resulting string will be NULL terminated, even if the description was
truncated in order to fit.

Some errors will generate additional descriptive
text, which will be assembled by this function into the designated buffer.
For this to work correctly, this function should be called directly following
the function that generated the error. Upon return of this function, the
library's internal buffer that stores the additional descriptive data will
be cleared.

\param con Connection pointer.
\param err The error value to describe.
\param buf Buffer to place the description in.
\param len Length of the target buffer, inclusive of NULL termination.

\return Does not return a value.
*/
extern void pccc_errstr(PCCC *con, PCCC_RET_T err, char *buf, size_t len)
{
    size_t add_len;
    PCCC_PRIV *con_priv;
    con_priv = (PCCC_PRIV *)con->priv_data;
    switch (err) {
        case PCCC_SUCCESS:
            sprintf(buf, "%s", "Success");
            break;
        case PCCC_WREADY:
            sprintf(buf, "%s", "Success, data pending to be written to link layer");
            break;
        case PCCC_ENOCON:
            sprintf(buf, "%s", "Supplied connection pointer was invalid(NULL)");
            break;
        case PCCC_ECMD_NODELIVER:
            sprintf(buf, "%s", "Link layer service could not deliver command");
            break;
        case PCCC_ECMD_TIMEOUT:
            sprintf(buf, "%s", "Timed out awaiting a reply");
            break;
        case PCCC_ECMD_REPLY:
            sprintf(buf, "%s", "Reply contained an error");
            break;
        case PCCC_ECMD_NOBUF:
            sprintf(buf, "%s", "No message buffers available to process command");
            break;
        case PCCC_ELINK:
            sprintf(buf, "%s", "Link layer service connection error");
            break;
        case PCCC_EPARAM:
            sprintf(buf, "%s", "Invalid parameter specified");
            break;
        case PCCC_EFATAL:
            sprintf(buf, "%s", "Fatal error");
            break;
        case PCCC_EOVERFLOW:
            sprintf(buf, "%s", "Buffer overflow");
            break;
        default:
            sprintf(buf, "%s", "Unknown error");
            break;
    }
    add_len = len - strlen(buf);
    if (con_priv->errstr[0]) /* Additional descriptive text present. */
    {
        char append[PCCC_ERR_LEN];
        snprintf(append, PCCC_ERR_LEN, " : %s.", con_priv->errstr);
        strncat(buf, append, add_len);
    } else {
        strncat(buf, ".", add_len);
    }
    con_priv->errstr[0] = '\0';
    buf[len - 1] = '\0'; /* Make sure the result is NULL terminated. */
    return;
}

/*
 * Description : Allocates buffers for a new connection.
 *
 * Arguments :
 *
 * Return Value :
 */
static int alloc_bufs(PCCC_PRIV *p)
{
    p->sock_in = buf_new(BUF_SIZE);
    if (p->sock_in == NULL) return -1;
    p->sock_out = buf_new(BUF_SIZE);
    if (p->sock_out == NULL) {
        buf_free(p->sock_in);
        return -1;
    }
    p->msg_in = buf_new(BUF_SIZE);
    if (p->msg_in == NULL) {
        buf_free(p->sock_in);
        buf_free(p->sock_out);
        return -1;
    }
    return 0;
}

/*
 * Description : Frees a connection's allocated buffers.
 *
 * Arguments :
 *
 * Return Value : None.
 */
static void free_bufs(PCCC_PRIV *p)
{
    buf_free(p->sock_in);
    buf_free(p->sock_out);
    buf_free(p->msg_in);
    return;
}

/*
 * Description : Resolves the server's hostname.
 *
 * Arguments : addr - Address structure to initalize with resolved address.
 *             hostname - Hostname to resolve.
 *             port - TCP port to use.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the hostname could not be resolved.
 */
static int get_addr(struct sockaddr_in *addr, const char *hostname, in_port_t port)
{
    struct hostent *host_entry;
    host_entry = gethostbyname(hostname);
    if (host_entry == NULL) return -1;
    addr->sin_family = host_entry->h_addrtype;
    addr->sin_port = htons(port);
    memcpy(&addr->sin_addr, *host_entry->h_addr_list, host_entry->h_length);
    return 0;
}

/*
 * Description : Sends a client registration to the link layer service.
 *
 * Arguments : con - Link layer connection pointer.
 *             name - Name to register with server.
 *
 * Return Value : PCCC_SUCCESS if no error occured.
 *                PCCC_EPARAM if the client name was invalid.
 *                PCCC_ELINK if an error occured writing the registration
 *                           message to the link layer connection.
 */
static PCCC_RET_T send_reg(PCCC *con, const char *name)
{
    size_t len;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    buf_append_byte(con_priv->sock_out, con->src_addr);
    if (name == NULL) {
        sprintf(con_priv->errstr, "%s", "Invalid pointer(NULL) to client name");
        return PCCC_EPARAM;
    }
    len = strlen(name);
    if (!len) {
        sprintf(con_priv->errstr, "%s", "Client name cannot be emtpy");
        return PCCC_EPARAM;
    }
    if (len > PCCC_NAME_LEN) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Client name too long, %u characters max", PCCC_NAME_LEN);
        return PCCC_EPARAM;
    }
    buf_append_byte(con_priv->sock_out, len);
    buf_append_str(con_priv->sock_out, name);
    if (pccc_write(con) != PCCC_SUCCESS) {
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Failed to send registration message : %s", strerror(errno));
        return PCCC_ELINK;
    }
    return PCCC_SUCCESS;
}

/*
 * Description : Parses data received from the link layer.
 *
 * Arguments : con - Link layer connection pointer.
 *
 * Return Value : PCCC_SUCCESS if no error occured.
 *                PCCC_EFATAL if a fatal error occured.
 */
static PCCC_RET_T parse_link(PCCC *con)
{
    uint8_t byte;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    while (!buf_get_byte(con_priv->sock_in, &byte)) {
        switch (con_priv->read_mode) {
            case READ_MODE_IDLE:
                switch (byte) {
                    case MSG_SOH:
                        buf_empty(con_priv->msg_in);
                        con_priv->read_mode = READ_MODE_MSG_LEN;
                        break;
                    case MSG_ACK:
                        if (rcv_ack(con)) return PCCC_EFATAL;
                        break;
                    case MSG_NAK:
                        rcv_nak(con);
                        break;
                }
                break;
            case READ_MODE_MSG_LEN:
                con_priv->msg_in_len = byte;
                con_priv->read_mode = READ_MODE_MSG;
                break;
            case READ_MODE_MSG:
                buf_append_byte(con_priv->msg_in, byte);
                if (con_priv->msg_in_len == con_priv->msg_in->len) {
                    con_priv->read_mode = READ_MODE_IDLE;
                    parse_msg(con);
                }
                break;
        }
    }
    return PCCC_SUCCESS;
}

/*
 * Description : Parses a message received from the link layer.
 *
 * Arguments : con - Link layer connection pointer.
 *
 * Return Value : None.
 */
static void parse_msg(PCCC *con)
{
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    if (msg_is_reply(con_priv)) {
        DF1MSG *msg = msg_find_cmd(con_priv);
        buf_append_byte(con_priv->sock_out, MSG_ACK);
        if (msg != NULL) {
            msg->state |= MSG_REPLY_RCVD;
            if (msg->notify == NULL) return;
            con_priv->msg_in->index = 6; /* Set buffer index to first data byte . */
            msg->result =
                (sts_check(con, con_priv->msg_in)
                || (msg->reply
                && msg->reply(con_priv->msg_in, msg, con_priv->errstr)))
                ? PCCC_ECMD_REPLY : PCCC_SUCCESS;
            if (msg->state == MSG_CMD_DONE) {
                msg_flush(msg);
                msg->notify(con, msg->result, msg->udata);
            }
            /*
             * If the ACK for the command message hasn't been received yet, and
             * the command resulted in an error, store the error string until
             * the ACK is received.
             */
            else if (msg->result != PCCC_SUCCESS)
                strcpy(msg->errstr, con_priv->errstr);
        }
    } else /* Message is a command. */
    {

    }
    return;
}

/*
 * Description : Accepts the link layer acknowledging the successful
 *               transmission of a message.
 *
 * Arguments : con - Link layer connection pointer.
 *
 * Return Value : Zero upon success.
 *                Negative one if time() failed.
 */
static int rcv_ack(PCCC *con)
{
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    DF1MSG *cur = con_priv->cur_msg;
    time_t now;
    cur->state |= MSG_ACK_RCVD;
    if (cur->is_cmd) {
        /*
         * For non-blocking mode, after a command is acknowledged,
         * set its expiration based on the timeout selection.
         */
        if (cur->notify) {
            now = time(NULL);
            if (now == -1) {
                snprintf(con_priv->errstr, PCCC_ERR_LEN, "time() failed : %s.", strerror(errno));
                return -1;
            }
            cur->expires = now + con->timeout + 1;
        }
        /*
         * This is in case the ACK for the command is received after the
         * reply has already been received.
         */
        if (cur->state == MSG_CMD_DONE) {
            msg_flush(cur);
            if (cur->result != PCCC_SUCCESS)
                strcpy(con_priv->errstr, cur->errstr);
            cur->notify(con, cur->result, cur->udata);
        }
    }
    /*
     * Acknowledged message was a reply.
     */
    else msg_flush(cur);
    msg_send_next((PCCC_PRIV *)con->priv_data);
    return 0;
}

/*
 * Description : Handles a failure of the link layer to deliver a message.
 *
 * Arguments : con - Link layer connection pointer.
 *
 * Return Value : None.
 */
static void rcv_nak(PCCC *con)
{
    DF1MSG *cur = ((PCCC_PRIV *)con->priv_data)->cur_msg;
    msg_flush(cur);
    if (cur->is_cmd && (cur->notify != NULL))
        cur->notify(con, PCCC_ECMD_NODELIVER, cur->udata);
    msg_send_next((PCCC_PRIV *)con->priv_data);
    return;
}

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

/**
Checks the integrity of transmissions over the communication link.
This command message transmits up to 243 bytes of data to a node interface
module. For SLC 500, SLC 5/01, and SLC 5/02 processors, maximum data size
is 95 bytes; for SLC 5/03 and SLC 5/04 processors, maximum data size is 236
bytes.

Compatibility as listed in Allen Bradley documentation:
- 1774-PLC
- MicroLogix 1000
- PLC-2
- PLC-3
- PLC-5
- PLC-5/250
- SLC-500
- SLC-5/03
- SLC-5/04

Tested with the following products:
- 1771-KE, Firmware Revision J, Series A
- 1785-KE, Firmware Revision H, Series B
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0
- PLC-5/30, Series C, Firmware Revision B
- SLC-500, Series B, FRN 6
- SLC-5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6
- Maximum seems to be 235 bytes. Anything larger and the link layer NAKs
the command instead of returning a reply with a STS error.

\param udata Pointer to data to send and compare response to.
\param bytes Size of user data. Must be non-zero.

\return
- PCCC_EPARAM if either the udata or bytes parameter was invalid.
*/
extern PCCC_RET_T pccc_cmd_Echo(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, size_t bytes)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (bytes > 243) {
        strncpy(con_priv->errstr, "Number of bytes too large", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (!bytes) {
        strncpy(con_priv->errstr, "Number of bytes must not be zero", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (udata == NULL) {
        strncpy(con_priv->errstr, "Udata cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    ret = cmd_init(con, &cmd, notify, reply_Echo, dnode, udata, 0x06, 0x00);
    if (ret != PCCC_SUCCESS) return ret;
    if (bytes && buf_append_blob(cmd->buf, udata, bytes)) {
        strncpy(con_priv->errstr, "pccc_cmd_Echo()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    cmd->bytes = bytes;
    return cmd_send(con, cmd);
}

/**
Sets the maximum NAKs, ENQs and timeout for an interface. Pretty much does
the work of pccc_cmd_SetTimeout(), pccc_cmd_SetNAKs() and pccc_cmd_SetENQs()
in one command.

Compatibility as listed in Allen Bradley documentation:
- PLC-2
- PLC-3
- PLC-5

Tested with the following products:
-

\param cycles Number of cycles to wait. See pccc_cmd_SetTimeout() for module
specific cycle information.
\param naks Number of NAKs.
\param enqs Number of ENQs.
*/
extern PCCC_RET_T pccc_cmd_SetVariables(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t cycles, uint8_t naks, uint8_t enqs)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x06, 0x02);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, cycles)
        || buf_append_byte(cmd->buf, naks)
        || buf_append_byte(cmd->buf, enqs)) {
        strncpy(con_priv->errstr, "pccc_cmd_SetVariables()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Sets the maximum amount of time the asynchronous interface module will wait
for an acknowledgement to a message transmission. This value is expressed in
cycles of the target module's internal clock.

The following table lists the cycle frequency and default setting for various
modules:
- 1770-KF2, 40 cyc/sec, 128
- 1771-KE/KF, 40 cyc/sec, 128
- 1771-KG, 38 cyc/sec, 38
- 1771-KGM, 63 cyc/sec, 63
- 1775-KA, 40 cyc/sec, 128
- 1785-KE, 29 cyc/sec, 128

Compatibility as listed in Allen Bradley documentation:
- PLC-2
- PLC-3
- PLC-5

Tested with the following products:
-

\param cycles Number of cycles to wait.
*/
extern PCCC_RET_T pccc_cmd_SetTimeout(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t cycles)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x06, 0x04);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, cycles)) {
        strncpy(con_priv->errstr, "pccc_cmd_SetTimeout()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Sets the maximum number of NAKs that the asynchronous interface module
accepts per message transmission.

Compatibility as listed in Allen Bradley documentation:
- PLC-2
- PLC-3
- PLC-5
- PLC-5/250

Tested with the following products:
-

\param naks Number of NAKs.
*/
extern PCCC_RET_T pccc_cmd_SetNAKs(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t naks)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x06, 0x05);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, naks)) {
        strncpy(con_priv->errstr, "pccc_cmd_SetNAKs()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Sets the maximum number of ENQs that the asynchronous interface module issues
per message transmission.

Compatibility as listed in Allen Bradley documentation:
- PLC-2
- PLC-3
- PLC-5
- PLC-5/250

Tested with the following products:
-

\param enqs Number of ENQs.
*/
extern PCCC_RET_T pccc_cmd_SetENQs(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t enqs)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x06, 0x06);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, enqs)) {
        strncpy(con_priv->errstr, "pccc_cmd_ENQs()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Reads the 'Maximum Solicit Address' DH485 parameter. This value is the maximum
node address that a DH485 node will try to solicit onto the link.

Compatibility as listed in Allen Bradley documentation:
- SLC-500
- SLC-5/03
- SLC-5/04 Channel 0 configured for DH485.

Tested with the following products:
-

\param udata Pointer to location to store the retrieved parameter value.
*/
extern PCCC_RET_T pccc_cmd_ReadLinkParam(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t *udata)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (udata == NULL) {
        strncpy(con_priv->errstr, "Udata cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    ret = cmd_init(con, &cmd, notify, reply_ReadLinkParam, dnode, (void *)udata, 0x06, 0x09);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_word(cmd->buf, 0) /* Address */
        || buf_append_byte(cmd->buf, 1)) /* Size */
    {
        strncpy(con_priv->errstr, "pccc_cmd_ReadLinkParam()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Sets the DH485 parameter maximum solicit address.
This parameter specifies the maximum node address that a DH485
node tries to solicit onto the link.

Compatibility as listed in Allen Bradley documentation:
- SLC-500
- SLC-5/03
- SLC-5/04 Channel 0 configured for DH485.

Tested with the following products:
-
*/
extern PCCC_RET_T pccc_cmd_SetLinkParam(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t max)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x06, 0x0a);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_word(cmd->buf, 0) /* Address */
        || buf_append_byte(cmd->buf, 1) /* Size */
        || buf_append_byte(cmd->buf, max)) {
        strncpy(con_priv->errstr, "pccc_cmd_SetLinkParam()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

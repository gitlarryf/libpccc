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

static void ext_sts(const BUF *msg, char *str);
static void ext_sts_dh(const BUF *msg, char *str, uint8_t es);
static void ext_sts_485(const BUF *msg, char *str, uint8_t es, uint8_t cmd);
static void ext_sts_1a(const BUF *msg, char *str);
static void ext_sts_1b(const BUF *msg, char *str);

/*
* Description : Evaluates the STS code in a reply message. If it indicates
*               an error(non-zero), a description of the error is placed
*               in the error string.
*
* Arguments : con - Link layer connection pointer.
*             msg - Pointer to reply message to query.
*
* Return Value : Zero if the STS indicates success.
*                Non-zero if the STS returned an error.
*/
extern int sts_check(PCCC *con, const BUF *msg)
{
    char str[PCCC_ERR_LEN];
    int remote = 0;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    uint8_t sts = msg_get_sts(msg);
    if (!sts) return 0;
    switch (sts) {
        /*
         * Local STS error codes.
         */
        case 0x01:
            strncpy(str, "Destination node is out of buffer space", PCCC_ERR_LEN);
            break;
        case 0x02:
            strncpy(str, "Cannot guarantee delivery, link layer", PCCC_ERR_LEN);
            break;
        case 0x03:
            strncpy(str, "Duplicate token holder detected", PCCC_ERR_LEN);
            break;
        case 0x04:
            strncpy(str, "Local port is disconnected", PCCC_ERR_LEN);
            break;
        case 0x05:
            strncpy(str, "Application layer timed out waiting for response", PCCC_ERR_LEN);
            break;
        case 0x06:
            strncpy(str, "Duplicate node detected", PCCC_ERR_LEN);
            break;
        case 0x07:
            strncpy(str, "Station is offline", PCCC_ERR_LEN);
            break;
        case 0x08:
            strncpy(str, "Hardware fault", PCCC_ERR_LEN);
            break;
            /*
             * Remote STS error codes.
             */
        case 0x10:
            strncpy(str, "Illegal command or format", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x20:
            strncpy(str, "Host has a problem and will not communicate", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x30:
            strncpy(str, "Remote node host is missing, disconnected, or shut down", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x40:
            strncpy(str, "Host could not complete function due to hardware fault", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x50:
            strncpy(str, "Addressing problem or memory protect rungs", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x60:
            strncpy(str, "Function not allowed due to command protection selection", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x70:
            strncpy(str, "Processor is in program mode", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x80:
            strncpy(str, "Compatibility mode file missing or communication zone problem", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0x90:
            strncpy(str, "Remote node cannot buffer command", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0xa0:
        case 0xc0:
            strncpy(str, "Wait ACK", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0xb0:
            strncpy(str, "Remote node problem due to download", PCCC_ERR_LEN);
            remote = 1;
            break;
        case 0xf0: /* EXT STS present. */
            ext_sts(msg, str);
            remote = 1;
            break;
        default:
            snprintf(str, PCCC_ERR_LEN, "Undefined STS 0x%x", sts);
            break;
    }
    str[PCCC_ERR_LEN - 1] = 0; /* Guarantee NULL termination. */
    snprintf(con_priv->errstr, PCCC_ERR_LEN, "%s node %u(dec) error : %s", remote ? "Remote" : "Local", msg_get_src(msg), str);
    return 1;
}

/*
* Description : Examines the originating CMD value and generates a
*               description of the EXT STS.
*
* Arguments : msg - Pointer to message to examine.
*             str - Destination for descriptive text.
*
* Return Value : None.
*/
static void ext_sts(const BUF *msg, char *str)
{
    uint8_t cmd = msg_get_cmd(msg);
    uint8_t es = msg_get_ext_sts(msg);
    switch (cmd) {
        case 0x0f: /* DH/DH+ error codes. */
            ext_sts_dh(msg, str, es);
            break;
        case 0x0b: /* DH485 error codes. */
        case 0x1a:
        case 0x1b:
            ext_sts_485(msg, str, es, cmd);
            break;
        default: /* Other commands shouldn't return EXT STS values. */
            snprintf(str, PCCC_ERR_LEN, "CMD 0x%x returned unexpected EXT STS 0x%x", cmd, es);
            break;
    }
    return;
}

/*
* Description : Generates a description of an EXT STS code for CMD 0x0f.
*
* Arguments : msg -
*             str - Destination string for description.
*             es - EXT STS value.
*
* Return Value : None.
*/
static void ext_sts_dh(const BUF *msg, char *str, uint8_t es)
{
    switch (es) {
        case 0x01:
            strncpy(str, "A field has an illegal value", PCCC_ERR_LEN);
            break;
        case 0x02:
            strncpy(str, "Less levels specified in address than minimum for any address", PCCC_ERR_LEN);
            break;
        case 0x03:
            strncpy(str, "More levels specified in address than system supports", PCCC_ERR_LEN);
            break;
        case 0x04:
            strncpy(str, "Symbol not found", PCCC_ERR_LEN);
            break;
        case 0x05:
            strncpy(str, "Symbol is of improper format", PCCC_ERR_LEN);
            break;
        case 0x06:
            strncpy(str, "Address doesn't point to something usable", PCCC_ERR_LEN);
            break;
        case 0x07:
            strncpy(str, "File is wrong size", PCCC_ERR_LEN);
            break;
        case 0x08:
            strncpy(str, "Cannot complete request, situation has changed since start of the command", PCCC_ERR_LEN);
            break;
        case 0x09:
            strncpy(str, "Data or file is too large", PCCC_ERR_LEN);
            break;
        case 0x0a:
            strncpy(str, "Transaction size plus word address is too large", PCCC_ERR_LEN);
            break;
        case 0x0b:
            strncpy(str, "Access denied, improper privilege", PCCC_ERR_LEN);
            break;
        case 0x0c:
            strncpy(str, "Condition cannot be generated, resource is not available", PCCC_ERR_LEN);
            break;
        case 0x0d:
            strncpy(str, "Condition already exists, resource is already available", PCCC_ERR_LEN);
            break;
        case 0x0e:
            strncpy(str, "Command cannot be executed", PCCC_ERR_LEN);
            break;
        case 0x0f:
            strncpy(str, "Histogram overflow", PCCC_ERR_LEN);
            break;
        case 0x10:
            strncpy(str, "No access", PCCC_ERR_LEN);
            break;
        case 0x11:
            strncpy(str, "Illegal data type", PCCC_ERR_LEN);
            break;
        case 0x12:
            strncpy(str, "Invalid parameter or invalid data", PCCC_ERR_LEN);
            break;
        case 0x13:
            strncpy(str, "Address reference exists to deleted area", PCCC_ERR_LEN);
            break;
        case 0x14:
            strncpy(str, "Command execution failure for unknown reason", PCCC_ERR_LEN);
            break;
        case 0x15:
            strncpy(str, "Data conversion errorr", PCCC_ERR_LEN);
            break;
        case 0x16:
            strncpy(str, "Scanner not able to communicate with 1771 rack adapter", PCCC_ERR_LEN);
            break;
        case 0x17:
            strncpy(str, "Type mismatch", PCCC_ERR_LEN);
            break;
        case 0x18:
            strncpy(str, "1771 module response was not valid", PCCC_ERR_LEN);
            break;
        case 0x19:
            strncpy(str, "Duplicate label", PCCC_ERR_LEN);
            break;
        case 0x1a:
            ext_sts_1a(msg, str);
            break;
        case 0x1b:
            ext_sts_1b(msg, str);
            break;
        case 0x1e:
            strncpy(str, "Data table element protection violation", PCCC_ERR_LEN);
            break;
        case 0x1f:
            strncpy(str, "Temporary internal problem", PCCC_ERR_LEN);
            break;
        case 0x22:
            strncpy(str, "Remote rack fault", PCCC_ERR_LEN);
            break;
        case 0x23:
            strncpy(str, "Timeout", PCCC_ERR_LEN);
            break;
        case 0x24:
            strncpy(str, "Unknown error", PCCC_ERR_LEN);
            break;
        default:
            snprintf(str, PCCC_ERR_LEN, "Undefined EXT STS 0x%x for CMD 0x0f", es);
            break;
            return;
    }
    return;
}

/*
* Description : Generates a description of an EXT STS code for CMDs 0x0b,
*               0x1a and 0x1b.
*
* Arguments : str - Destination string for description.
*             es - EXT STS value.
*             cmd - CMD value.
*
* Return Value : None.
*/
static void ext_sts_485(const BUF *msg, char *str, uint8_t es, uint8_t cmd)
{
    switch (es) {
        case 0x07:
            strncpy(str, "Insufficient memory module size", PCCC_ERR_LEN);
            break;
        case 0x0b:
            strncpy(str, "Access denied, priviledge violation", PCCC_ERR_LEN);
            break;
        case 0x0c:
            strncpy(str, "Resouce not available or can not do", PCCC_ERR_LEN);
            break;
        case 0x0e:
            strncpy(str, "CMD can not be executed", PCCC_ERR_LEN);
            break;
        case 0x12:
            strncpy(str, "Invalid parameter", PCCC_ERR_LEN);
            break;
        case 0x14:
            strncpy(str, "Failure during processing", PCCC_ERR_LEN);
            break;
        case 0x19:
            strncpy(str, "Duplicate label", PCCC_ERR_LEN);
            break;
        case 0x1a:
            ext_sts_1a(msg, str);
            break;
        case 0x1b:
            ext_sts_1b(msg, str);
            break;
        default:
            snprintf(str, PCCC_ERR_LEN, "Undefined EXT STS 0x%x for CMD 0x%x", es, cmd);
            break;
    }
    return;
}

/*
* Description : Generates a description of EXT STS value 0x1a. Includes
*               the owner node if present.
*
* Arguments : msg - Pointer to message containing EXT STS of 0x1a.
*             str - Destination string for description.
*
* Return Value : None.
*/
static void ext_sts_1a(const BUF *msg, char *str)
{
    uint8_t on; /* Owner node */
    if (msg_get_owner_node(msg, &on))
        snprintf(str, PCCC_ERR_LEN, "File is open; node %u owns it.  For SLC 5/05 node 256 indicates the Ethernet port", on);
    else
        strncpy(str, "File is open; another node owns it", PCCC_ERR_LEN);
    return;
}

/*
* Description : Generates a description of EXT STS value 0x1b. Includes
*               the owner node if present.
*
* Arguments : msg - Pointer to message containing EXT STS of 0x1b.
*             str - Destination string for description.
*
* Return Value : None.
*/
static void ext_sts_1b(const BUF *msg, char *str)
{
    uint8_t on; /* Owner node */
    if (msg_get_owner_node(msg, &on))
        snprintf(str, PCCC_ERR_LEN, "Node %u is the program owner.  For SLC 5/05 node 256 indicates the Ethernet port", on);
    else
        strncpy(str, "Another node is the program owner", PCCC_ERR_LEN);
    return;
}

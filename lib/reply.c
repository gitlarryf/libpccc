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
* Description : Reply handler for the echo command. Compares the received data
*               with the original source data.
*
* Arguments : rply - Pointer to a buffer containing the reply.
*             cmd - Pointer to the original command message.
*             err - Pointer to a string to hold any possible error messages.
*
* Return Value : Zero if the reply was parsed successfully.
*                Non-zero if an error occured.
*/
extern int reply_Echo(BUF *rply, DF1MSG *cmd, char *err)
{
    size_t bytes = rply->len - 6;
    if (bytes != cmd->bytes) {
        strncpy(err, "Number of received bytes doesn't match number of bytes sent", PCCC_ERR_LEN);
        return -1;
    }
    if (memcmp(cmd->udata, (void *)&rply->data[6], bytes)) {
        strncpy(err, "Received data mismatch", PCCC_ERR_LEN);
        return -1;
    }
    return 0;
}

/*
* Description : Reply handler for the protected typed logical read commands.
*
* Arguments : rply - Pointer to a buffer containing the reply.
*             cmd - Pointer to the original command message.
*             err - Pointer to a string to hold any possible error messages.
*
* Return Value : Zero if the reply was parsed successfully.
*                Non-zero if an error occured.
*/
extern int reply_ProtectedTypedLogicalRead(BUF *rply, DF1MSG *cmd, char *err)
{
    if (cmd->bytes != msg_get_len(rply)) {
        strncpy(err, "Received unexpected amount of data", PCCC_ERR_LEN);
        return -1;
    }
    return data_dec_array(rply, cmd, err);
}

/*
* Description : Reply handler for read SLC file info.
*
* Arguments : rply - Pointer to a buffer containing the reply.
*             cmd - Pointer to the original command message.
*             err - Pointer to a string to hold any possible error messages.
*
* Return Value : Zero if the reply was parsed successfully.
*                Non-zero if an error occured.
*/
extern int reply_ReadSLCFileInfo(BUF *rply, DF1MSG *cmd, char *err)
{
    PCCC_SLC_FI_T *p = (PCCC_SLC_FI_T *)cmd->udata;
    uint8_t dt;
    uint32_t bytes;
    uint16_t elements;
    if (msg_get_len(rply) != 8) {
        strncpy(err, "Received unexpected amount of data", PCCC_ERR_LEN);
        return -1;
    }
    buf_get_long(rply, &bytes);
    buf_get_word(rply, &elements);
    p->bytes = ltohl(bytes);
    p->elements = ltohs(elements);
    buf_get_byte(rply, &dt); /* Reserved byte. */
    buf_get_byte(rply, &dt);
    switch (dt) {
        case 0x82:
            p->type = PCCC_FT_OUT;
            break;
        case 0x83:
            p->type = PCCC_FT_IN;
            break;
        case 0x84:
            p->type = PCCC_FT_STAT;
            break;
        case 0x85:
            p->type = PCCC_FT_BIN;
            break;
        case 0x86:
            p->type = PCCC_FT_TIMER;
            break;
        case 0x87:
            p->type = PCCC_FT_COUNT;
            break;
        case 0x88:
            p->type = PCCC_FT_CTL;
            break;
        case 0x89:
            p->type = PCCC_FT_INT;
            break;
        case 0x8a:
            p->type = PCCC_FT_FLOAT;
            break;
        case 0x8d:
            p->type = PCCC_FT_STR;
            break;
        case 0x8e:
            p->type = PCCC_FT_ASC;
            break;
        case 0x8f:
            p->type = PCCC_FT_BCD;
            break;
        default:
            snprintf(err, PCCC_ERR_LEN, "Received unknown file type - 0x%x", dt);
            return -1;
            break;
    }
    return 0;
}

/*
* Description :
*
* Arguments : rply - Pointer to a buffer containing the reply.
*             cmd - Pointer to the original command message.
*             err - Pointer to a string to hold any possible error messages.
*
* Return Value : Zero if the reply was parsed successfully.
*                Non-zero if an error occured.
*/
extern int reply_ReadLinkParam(BUF *rply, DF1MSG *cmd, char *err)
{
    if (msg_get_len(rply) != 1) {
        strncpy(err, "Received unexpected amount of data", PCCC_ERR_LEN);
        return -1;
    }
    buf_get_byte(rply, (uint8_t *)cmd->udata);
    return 0;
}

/*
* Description : Reply handler that prints out the data from a reply. Only used
*               for troubleshooting and creating new reply handlers.
*
* Arguments : rply - Pointer to a buffer containing the reply.
*             cmd - Pointer to the original command message.
*             err - Pointer to a string to hold any possible error messages.
*
* Return Value : Always zero.
*/
extern int reply_Dummy(BUF *rply, DF1MSG *cmd, char *err)
{
    uint8_t byte;
    rply->index = 0;
    while (!buf_get_byte(rply, &byte))
        printf("0x%x ", byte);
    printf("\n");
    return 0;
}

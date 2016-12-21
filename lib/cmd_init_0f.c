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

static PCCC_RET_T ptl_init(PCCC *con, DF1MSG **pm, UFUNC notify, uint8_t dnode,
                           void *udata, uint8_t func, PCCC_FT_T file_type,
                           uint16_t file, uint16_t element,
                           uint16_t sub_element, size_t num_elements);

/**
Modifies specified bits in a single word.

Compatibility as listed in Allen Bradley documentation:
- PLC-3
- PLC-5
- PLC-5/250

Tested with the following products:
- PLC-5/30, Series C, Firmware Revision B

\param addr - Pointer to a PLC address of the data to modify.
This address must point to an element containing a sixteen bit word.
\param set - Bits set in this mask will be set in the target word.
\param reset - Bits set in this mask will be reset in the target word.
Specifying mask values which have common bits set in both masks will generate
an error.

\return
- PCCC_EPARAM if the one of the supplied parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_BitWrite(PCCC *con, UFUNC notify, uint8_t dnode, const PCCC_PLC_ADDR *addr, uint16_t set, uint16_t reset)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (addr == NULL) {
        strncpy(con_priv->errstr, "Address pointer cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (set & reset) {
        strncpy(con_priv->errstr, "Bits must be mutually exclusive in masks", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x02);
    if (ret != PCCC_SUCCESS) return ret;
    ret = addr_enc_plc(cmd->buf, addr, con_priv->errstr);
    if (ret != PCCC_SUCCESS) {
        msg_flush(cmd);
        return ret;
    }
    if (buf_append_word(cmd->buf, set) || buf_append_word(cmd->buf, reset)) {
        strncpy(con_priv->errstr, "pccc_cmd_BitWrite()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Changes the mode of a MicroLogix processor.

Compatibility as listed in Allen Bradley documentation:
- MicroLogix 1000

Tested with the following products:
-

\param mode One of the following \ref PCCC_MODE_T "PCCC_MODE_T" enumerations:
- PCCC_MODE_RUN
- PCCC_MODE_PROG

\return
- PCCC_EPARAM if the given mode was invalid.
*/
extern PCCC_RET_T pccc_cmd_ChangeModeMicroLogix1000(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    uint8_t mode_val;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    switch (mode) {
        case PCCC_MODE_PROG:
            mode_val = 0x01;
            break;
        case PCCC_MODE_RUN:
            mode_val = 0x02;
            break;
        default:
            strncpy(con_priv->errstr, "Command does not support selected processor mode", PCCC_ERR_LEN);
            return PCCC_EPARAM;
            break;
    }
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x3a);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, mode_val)) {
        strncpy(con_priv->errstr, "pccc_cmd_ChangeModeMicroLogix1000()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Changes the mode of a SLC processor. For a SLC 5/03 or 5/04 processor,
change mode only works when the keyswitch is in the REM position.

Compatibility as listed in Allen Bradley documentation:
- SLC 500
- SLC 5/03
- SLC 5/04

Tested with the following products:
- SLC-500, Series B, FRN 6
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0
- Doesn't support PCCC_MODE_TEST_DEBUG.

\param mode One of the following \ref PCCC_MODE_T "PCCC_MODE_T" enumerations:
- PCCC_MODE_PROG
- PCCC_MODE_RUN
- PCCC_MODE_TEST_CONT
- PCCC_MODE_TEST_SINGLE
- PCCC_MODE_TEST_DEBUG Not available with SLC 500 and SLC 5/01.

\return
- PCCC_EPARAM if the given mode was invalid.
*/
extern PCCC_RET_T pccc_cmd_ChangeModeSLC500(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    uint8_t mode_val;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    switch (mode) {
        case PCCC_MODE_PROG:
            mode_val = 0x01;
            break;
        case PCCC_MODE_RUN:
            mode_val = 0x06;
            break;
        case PCCC_MODE_TEST_CONT:
            mode_val = 0x07;
            break;
        case PCCC_MODE_TEST_SINGLE:
            mode_val = 0x08;
            break;
        case PCCC_MODE_TEST_DEBUG:
            mode_val = 0x09;
            break;
        default:
            strncpy(con_priv->errstr, "Command does not support selected processor mode", PCCC_ERR_LEN);
            return PCCC_EPARAM;
            break;
    }
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x80);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, mode_val)) {
        strncpy(con_priv->errstr, "pccc_cmd_ChangeModeSLC500()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Reads data from a logical address in a SLC 500 processor.

Compatibility as listed in Allen Bradley documentation:
- MicroLogix 1000
- SLC 500
- SLC 5/03
- SLC 5/04
- PLC-5 This may be a typo as it doesn't work with the PLC-5/30
that I have for testing.

Tested with the following products:
- SLC-500, Series B, FRN 6
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0

\param udata Location to store received data.
\param file_type One of the PCCC_FT_T enumerations. Supported data types are:
- PCCC_FT_INT
- PCCC_FT_BIN
- PCCC_FT_TIMER
- PCCC_FT_COUNT
- PCCC_FT_CTL
- PCCC_FT_FLOAT
- PCCC_FT_STR
- PCCC_FT_STAT
\param file Source address file number.
\param element Source address element number.
\param sub_element Source address subelement number. Reading subelement
members of structured elements is not currently supported with this command.
Must be set to zero.
\param num_elements Number of elements to read. Total quantity of bytes
transferred is limited to:
- SLC 5/01 82 bytes
- SLC 5/02 82 bytes
- SLC 5/03 236 bytes
- SLC 5/04 236 bytes

\return
PCCC_EPARAM - If one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalRead3AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, 
                                                                   PCCC_FT_T file_type, uint16_t file, uint16_t element, 
                                                                   uint16_t sub_element, size_t num_elements)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    ret = ptl_init(con, &cmd, notify, dnode, udata, 0xa2, file_type, file, element, sub_element, num_elements);
    if (ret != PCCC_SUCCESS) return ret;
    return cmd_send(con, cmd);
}

/**
Reads data from a logical address in a SLC 500 processor. Pretty much the same
as pccc_cmd_ProtectedTypedLogicalRead3AddressFields() without the subelement
parameter, however it is not supported as widely.

Compatibility as listed in Allen Bradley documentation:
- Not specified.

Tested with the following products:
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0
- SLC-500, Series B, FRN 6
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6

\param udata Location to store received data.
\param file_type One of the following PCCC_FT_T enumerations:
- PCCC_FT_INT
- PCCC_FT_BIN
- PCCC_FT_TIMER
- PCCC_FT_COUNT
- PCCC_FT_CTL
- PCCC_FT_FLOAT
- PCCC_FT_STR
- PCCC_FT_STAT
\param file Source address file number.
\param element Source address element number.
\param num_elements Number of elements to read. Total quantity of bytes
transferred is limited to:
- SLC 5/01 82 bytes
- SLC 5/02 82 bytes
- SLC 5/03 236 bytes
- SLC 5/04 236 bytes

\return
PCCC_EPARAM - If one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalRead2AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, 
                                                                   PCCC_FT_T file_type, uint16_t file, uint16_t element, size_t num_elements)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    ret = ptl_init(con, &cmd, notify, dnode, udata, 0xa1, file_type, file, element, 0, num_elements);
    if (ret != PCCC_SUCCESS) return ret;
    return cmd_send(con, cmd);
}

/**
Writes data to a logical address in a SLC 500 processor.

Compatibility as listed in Allen Bradley documentation:
- MicroLogix 1000
- SLC 500
- SLC 5/03
- SLC 5/04
- PLC-5 This may be a typo as it doesn't work with the PLC-5/30
that I have for testing.

Tested with the following products:
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6

\param udata Source data array.
\param file_type One of the PCCC_FT_T enumerations. Supported data types are:
- PCCC_FT_INT
- PCCC_FT_BIN
- PCCC_FT_TIMER
- PCCC_FT_COUNT
- PCCC_FT_CTL
- PCCC_FT_FLOAT
- PCCC_FT_STR
- PCCC_FT_STAT
\param file Target address file number.
\param element Target address element number.
\param sub_element Target address subelement number. Writing subelement
members of structured elements is not currently supported with this command.
Must be zero.
\param num_elements Number of elements to read. Total quantity of bytes
transferred is limited to:
- SLC 5/01 82 bytes
- SLC 5/02 82 bytes
- SLC 5/03 234 bytes
- SLC 5/04 234 bytes

\return
PCCC_EPARAM - If one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWrite3AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, 
                                                                    PCCC_FT_T file_type, uint16_t file, uint16_t element, 
                                                                    uint16_t sub_element, size_t num_elements)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    ret = ptl_init(con, &cmd, notify, dnode, udata, 0xaa, file_type, file, element, sub_element, num_elements);
    if (ret != PCCC_SUCCESS) return ret;
    ret = data_enc_array(cmd, ((PCCC_PRIV *)con->priv_data)->errstr);
    if (ret != PCCC_SUCCESS) {
        msg_flush(cmd);
        return ret;
    }
    return cmd_send(con, cmd);
}

/**
Writes data to a logical address in a SLC 500 processor. Pretty much the same
as pccc_cmd_ProtectedTypedLogicalWrite3AddressFields() without the subelement
parameter, however it is not supported as widely.

Compatibility as listed in Allen Bradley documentation:
- Not specified.

Tested with the following products:
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6

\param udata Source data array.
\param file_type One of the PCCC_FT_T enumerations. Supported data types are:
- PCCC_FT_INT
- PCCC_FT_BIN
- PCCC_FT_TIMER
- PCCC_FT_COUNT
- PCCC_FT_CTL
- PCCC_FT_FLOAT
- PCCC_FT_STR
- PCCC_FT_STAT
\param file Target address file number.
\param element Target address element number.
\param num_elements Number of elements to read. Total quantity of bytes
transferred is limited to:
- SLC 5/01 82 bytes
- SLC 5/02 82 bytes
- SLC 5/03 234 bytes
- SLC 5/04 234 bytes

\return
PCCC_EPARAM - If one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWrite2AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, 
                                                                    PCCC_FT_T file_type, uint16_t file, uint16_t element, size_t num_elements)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    ret = ptl_init(con, &cmd, notify, dnode, udata, 0xa9, file_type, file, element, 0, num_elements);
    if (ret != PCCC_SUCCESS) return ret;
    ret = data_enc_array(cmd, ((PCCC_PRIV *)con->priv_data)->errstr);
    if (ret != PCCC_SUCCESS) {
        msg_flush(cmd);
        return ret;
    }
    return cmd_send(con, cmd);
}

/**
Sets the operating mode of the processor at the next I/O scan. Processor must
be in Remote mode.

Compatibility as listed in Allen Bradley documentation:
- MicroLogix 1000
- PLC-5
- PLC-5/250

Tested with the following products:
- PLC-5/30, Series C, Firmware Revision B
- MicroLogix 1200, Series C, Revision D, OS Cat Num 1200, Series C, FRN 7.0
- Doesn't support PCCC_MODE_REM_TEST.

\param mode One of the following \ref PCCC_MODE_T "PCCC_MODE_T" enumerations:
- PCCC_MODE_PROG
- PCCC_MODE_REM_TEST
- PCCC_MODE_REM_RUN

\return
- PCCC_EPARAM if the given mode was invalid.
*/
extern PCCC_RET_T pccc_cmd_SetCPUMode(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    uint8_t mode_val;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    switch (mode) {
        case PCCC_MODE_PROG:
            mode_val = 0x00;
            break;
        case PCCC_MODE_REM_TEST:
            mode_val = 0x01;
            break;
        case PCCC_MODE_REM_RUN:
            mode_val = 0x02;
            break;
        default:
            strncpy(con_priv->errstr, "Command does not support selected processor mode", PCCC_ERR_LEN);
            return PCCC_EPARAM;
            break;
    }
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x3a);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_byte(cmd->buf, mode_val)) {
        strncpy(con_priv->errstr, "pccc_cmd_SetCPUMode()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Determines a SLC file's type and size.

Compatibility as listed in Allen Bradley documentation:
- SLC-5/03
- SLC-5/04

Tested with the following products:
- SLC-5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6

\param udata Pointer to a \link pccc_slc_fi_t PCCC_SLC_FI_T \endlink
in which to store the result.
\param file_num File number to query.

\return
- PCCC_EPARAM if the udata pointer was invalid(NULL).
*/
extern PCCC_RET_T pccc_cmd_ReadSLCFileInfo(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_SLC_FI_T *udata, uint8_t file_num)
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
    ret = cmd_init(con, &cmd, notify, reply_ReadSLCFileInfo, dnode, (void *)udata, 0x0f, 0x94);
    if (ret != PCCC_SUCCESS) return ret;
    /*
    * Mask, major file type and file number follow.
    */
    if (buf_append_byte(cmd->buf, 0x06)
        /*
        * Major file type, 0x80 for data table file.
        */
        || buf_append_byte(cmd->buf, 0x80)
        || buf_append_byte(cmd->buf, file_num)) {
        strncpy(con_priv->errstr, "pccc_cmd_ReadSLCFileInfo()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    return cmd_send(con, cmd);
}

/**
Disables the forcing function for I/O. All forcing data is ignored,
but remains intact.

Compatibility as listed in Allen Bradley documentation:
- MicroLogix 1000
- PLC-5
- PLC-5/250
- SLC 500
- SLC 5/03
- SLC 5/04

Tested with the following products:
- PLC-5/30, Series C, Firmware Revision B
- SLC-500, Series B, FRN 6
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6
*/
extern PCCC_RET_T pccc_cmd_DisableForces(PCCC *con, UFUNC notify, uint8_t dnode)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    if (con == NULL) return PCCC_ENOCON;
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x41);
    if (ret != PCCC_SUCCESS) return ret;
    return cmd_send(con, cmd);
}

/**
Writes bit data to a logical address in a SLC processor by specifying a bit
mask and three address fields.

Compatibility as listed in Allen Bradley documentation:
- SLC

Tested with the following products:
- SLC-500, Series B, FRN 6
- SLC 5/05, Series B, Proc. Revision 4. OS Cat Num 501, Series C, FRN 6

\param udata Source data array.
\param mask Bit mask. Bit positions set to one in this mask will be modified
in the destination. This mask will be applied to all elements.
\param file_type One of the \ref PCCC_FT_T "PCCC_FT_T" enumerations. Supported data types are:
- PCCC_FT_INT
- PCCC_FT_BIN
- PCCC_FT_STAT
\param file Target address file number.
\param element Target address element number.
\param sub_element Target address subelement number. Writing subelement
members of structured elements is not currently supported with this command.
Must be zero.
\param num_elements Number of elements to read. Total quantity of bytes
transferred is limited to:
- SLC 5/01 82 bytes
- SLC 5/02 82 bytes
- SLC 5/03 234 bytes
- SLC 5/04 234 bytes

\return
PCCC_EPARAM If one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWriteWithMask(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, 
                                                              PCCC_BIN_T mask, PCCC_FT_T file_type, uint16_t file, 
                                                              uint16_t element, uint16_t sub_element, size_t num_elements)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    switch (file_type) {
        case PCCC_FT_INT:
        case PCCC_FT_BIN:
        case PCCC_FT_STAT:
            break;
        default:
            strncpy(con_priv->errstr, "File type not supported", PCCC_ERR_LEN);
            return PCCC_EPARAM;
    }
    ret = ptl_init(con, &cmd, notify, dnode, udata, 0xab, file_type, file,
                   element, sub_element, num_elements);
    if (ret != PCCC_SUCCESS) return ret;
    if (buf_append_word(cmd->buf, htols(mask))) {
        strncpy(con_priv->errstr, "pccc_cmd_ProtectedTypedLogicalWriteWithMask()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    ret = data_enc_array(cmd, ((PCCC_PRIV *)con->priv_data)->errstr);
    if (ret != PCCC_SUCCESS) {
        msg_flush(cmd);
        return ret;
    }
    return cmd_send(con, cmd);
}

/**
Sets or resets specified bits in specified words of data table memory. Each
word to be modified is specified in a set containing the address of the word,
the AND mask to apply, and the OR mask to apply. For example set 3 would be
comprised of the PLC address addr[2], AND mask and[2], and OR mask or[2].

The PLC processes each set in this fashion:
- The specified word is copied from the data table.
- Bits specified with the AND mask are reset.
- Bits specified with the OR mask are set.
- The word is written back to the data table.

It is possible that the controller may modify the state of the original
word before the execution of this command writes the modified data back. This
could cause data to be unintentionally overwritten. To avoid this, it is
suggested to use this command on words that the controller only reads from.

Compatibility as listed in Allen Bradley documentation:
- PLC-5
- PLC-5/VME

Tested with the following products:
- PLC-5/30, Series C, Firmware Revision B

\param addr A pointer to an array of PLC addresses.
Addresses must point to a sixteen bit word.
\param and A pointer to an array of sixteen bit AND masks.
A '0' in this mask will reset a bit, a '1' will set a leave it unchanged.
\param or A pointer to an array of sixteen bit OR masks.
A '1' in this mask will set a bit, a '0' will leave it unchanged.
\param sets The quantity of address/AND mask/OR mask sets. Must be non-zero.
The total encoded size of all the sets must be not exceed than 243 bytes. Due
to the various ways that the PLC addresses may be encoded, there isn't an easy
way to calculate this beforehand. If the result ends up being too large,
an error will be returned.

\return
PCCC_EPARAM if one of the parameters was invalid.
*/
extern PCCC_RET_T pccc_cmd_ReadModifyWrite(PCCC *con, UFUNC notify, uint8_t dnode, const PCCC_PLC_ADDR *addr, const uint16_t *and, const uint16_t *or, size_t sets)
{
    DF1MSG *cmd;
    PCCC_RET_T ret;
    PCCC_PRIV *con_priv;
    int i;
    if (con == NULL) return PCCC_ENOCON;
    con_priv = (PCCC_PRIV *)con->priv_data;
    if (addr == NULL) {
        strncpy(con_priv->errstr, "Address pointer cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (and == NULL) {
        strncpy(con_priv->errstr, "AND mask pointer cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (or == NULL) {
        strncpy(con_priv->errstr, "OR mask pointer cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (!sets) {
        strncpy(con_priv->errstr, "Number of sets must be non-zero", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    ret = cmd_init(con, &cmd, notify, NULL, dnode, NULL, 0x0f, 0x26);
    if (ret != PCCC_SUCCESS) return ret;
    for (i = 0; i < sets; i++) {
        size_t data_len;
        ret = addr_enc_plc(cmd->buf, addr + i, con_priv->errstr);
        if (ret != PCCC_SUCCESS) {
            msg_flush(cmd);
            return ret;
        }
        if (buf_append_word(cmd->buf, htols(and[i]))
            || buf_append_word(cmd->buf, htols(or[i]))) {
            strncpy(con_priv->errstr, "pccc_cmd_ReadModifyWrite()", PCCC_ERR_LEN);
            msg_flush(cmd);
            return PCCC_EOVERFLOW;
        }
        data_len = cmd->buf->len - 7;
        if (data_len > 243) {
            strncpy(con_priv->errstr, "Number of sets exceeded maximum command size", PCCC_ERR_LEN);
            msg_flush(cmd);
            return PCCC_EPARAM;
        }
    }
    return cmd_send(con, cmd);
}

/*
* Description : Initializes a 'protected typed logical read/write' command.
*
* Arguments :
*
* Return Value :
*/
static PCCC_RET_T ptl_init(PCCC *con, DF1MSG **pm, UFUNC notify, uint8_t dnode,
                           void *udata, uint8_t func, PCCC_FT_T file_type,
                           uint16_t file, uint16_t element,
                           uint16_t sub_element, size_t num_elements)
{
    DF1MSG *cmd;
    size_t bytes, usize, bytes_per_element;
    uint8_t ft_value;
    PCCC_RET_T ret;
    RFUNC reply;
    int overflow;
    PCCC_PRIV *con_priv = (PCCC_PRIV *)con->priv_data;
    if (udata == NULL) {
        strncpy(con_priv->errstr, "Udata cannot be NULL", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (sub_element) {
        strncpy(con_priv->errstr, "Nonzero subelement values not supported", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    /*
    * Write functions don't use a reply handler.
    */
    reply = (func == 0xa1) || (func == 0xa2) ?
reply_ProtectedTypedLogicalRead : NULL;
    switch (file_type) {
        case PCCC_FT_INT:
            usize = sizeof(PCCC_INT_T);
            bytes_per_element = PCCC_SO_INT;
            ft_value = 0x89;
            break;
        case PCCC_FT_BIN:
            bytes_per_element = PCCC_SO_BIN;
            usize = sizeof(PCCC_BIN_T);
            ft_value = 0x85;
            break;
        case PCCC_FT_TIMER:
            bytes_per_element = PCCC_SO_TIMER;
            usize = sizeof(PCCC_TIMER_T);
            ft_value = 0x86;
            break;
        case PCCC_FT_COUNT:
            bytes_per_element = PCCC_SO_COUNT;
            usize = sizeof(PCCC_COUNT_T);
            ft_value = 0x87;
            break;
        case PCCC_FT_CTL:
            bytes_per_element = PCCC_SO_CTL;
            usize = sizeof(PCCC_CTL_T);
            ft_value = 0x88;
            break;
        case PCCC_FT_FLOAT:
            bytes_per_element = PCCC_SO_FLOAT;
            usize = sizeof(PCCC_FLOAT_T);
            ft_value = 0x8a;
            break;
        case PCCC_FT_STR:
            bytes_per_element = PCCC_SO_STR;
            usize = sizeof(PCCC_STR_T);
            ft_value = 0x8d;
            break;
        case PCCC_FT_STAT:
            bytes_per_element = PCCC_SO_STAT;
            usize = sizeof(PCCC_STAT_T);
            ft_value = 0x84;
            break;
        default:
            strncpy(con_priv->errstr, "File type not supported", PCCC_ERR_LEN);
            return PCCC_EPARAM;
            break;
    }
    bytes = bytes_per_element * num_elements;
    if (bytes > 236) {
        int max_elements = 236 / bytes_per_element;
        snprintf(con_priv->errstr, PCCC_ERR_LEN, "Too many elements. Data type allows %u elements max", max_elements);
        return PCCC_EPARAM;
    }
    ret = cmd_init(con, &cmd, notify, reply, dnode, udata, 0x0f, func);
    if (ret != PCCC_SUCCESS) return ret;
    overflow = buf_append_byte(cmd->buf, bytes);
    overflow |= addr_encode(cmd->buf, file);
    overflow |= buf_append_byte(cmd->buf, ft_value);
    overflow |= addr_encode(cmd->buf, element);
    /*
    * Only commands with three address fields get the subelement.
    */
    if ((func == 0xa2) /* Read */
        || (func == 0xaa) /* Write */
        || (func == 0xab)) /* Write with mask */
        overflow |= addr_encode(cmd->buf, sub_element);
    if (overflow) {
        strncpy(con_priv->errstr, "ptl_init()", PCCC_ERR_LEN);
        msg_flush(cmd);
        return PCCC_EOVERFLOW;
    }
    cmd->elements = num_elements;
    cmd->usize = usize;
    cmd->bytes = bytes;
    cmd->file_type = file_type;
    *pm = cmd; /* Assign the new message buffer back to the calling command. */
    return PCCC_SUCCESS;
}

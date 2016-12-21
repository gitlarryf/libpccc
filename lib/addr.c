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

static PCCC_RET_T enc_plc_lba(BUF *dst, const PCCC_PLC_LBA *adr, char *err);
static int append_lvls(BUF *dst, uint8_t mask, const uint16_t *lvls);
static PCCC_RET_T enc_plc_laa(BUF *dst, const char *adr, char *err);

/*
* Description : Encodes an address element into a buffer.
*
* Arguments : dest - Target buffer.
*             addr - The address value to encode.
*
* Return Value : Zero if successful.
*                Non-zero if the target buffer could not hold the
*                encoded address.
*/
extern int addr_encode(BUF *dest, uint16_t addr)
{
    /*
    * Values greater than 254 are expanded into a three byte sequence
    * prefixed with a 0xff byte.
    */
    if (addr > 254)
        return buf_append_byte(dest, 0xff) || buf_append_word(dest, htols(addr));
    return buf_append_byte(dest, addr);
}

/*
* Description : Decodes an address value from a one or three byte encoded
*               value.
*
* Arguments : src - Source buffer.
*             addr - Pointer to place decoded address value into.
*
* Return Value : Zero if the address was successfully decoded.
*                Non-zero if the end of buffer was encountered before
*                the complete address could be decoded.
*/
extern int addr_decode(BUF *src, uint16_t *addr)
{
    uint8_t first_byte;
    if (buf_get_byte(src, &first_byte)) return -1;
    if (first_byte == 0xff) /* Three byte sequence. */
    {
        uint16_t word;
        if (buf_get_word(src, &word)) return -1;
        *addr = ltohs(word);
    } else *addr = first_byte;
    return 0;
}

/*
* Description : Encodes a PLC address into a buffer. This function
*               accepts any of the PLC logical address types.
*
* Arguments : dst - Pointer to the destination buffer.
*             src - Pointer to the address to encode.
*             err - Pointer to the location to store a descriptive error
*                   string.
*
* Return Value : PCCC_SUCCESS if the address was encoded successfully.
*                PCCC_EPARAM if the address was invalid.
*                PCCC_EOVERFLOW if the end of the buffer was reached before
*                               the address could be completely encoded.
*/
extern PCCC_RET_T addr_enc_plc(BUF *dst, const PCCC_PLC_ADDR *src, char *err)
{
    switch (src->type) {
        case PCCC_PLC_ADDR_BIN:
            return enc_plc_lba(dst, &src->addr.lba, err);
            break;
        case PCCC_PLC_ADDR_ASCII:
            return enc_plc_laa(dst, src->addr.ascii, err);
            break;
        default:
            sprintf(err, "%s", "Unknown PLC address type");
            break;
    }
    return PCCC_EPARAM;
}

/*
* Description : Encodes a PLC logical binary address into a buffer.
*
* Arguments : dst - Pointer to the destination buffer. The address will
*                   be appended to the end of the buffer.
*             adr - Pointer to the address to encode.
*             err - Pointer to the location to store a descriptive error
*                   string.
*
* Return Value : PCCC_SUCCESS if successful.
*                PCCC_EPARAM if the number of levels specified was invalid
*                            or if one of the level values was illegal.
*                PCCC_EOVERFLOW if the target buffer could not hold the
*                               encoded address.
*/
static PCCC_RET_T enc_plc_lba(BUF *dst, const PCCC_PLC_LBA *adr, char *err)
{
    register uint8_t mask = 0;
    size_t i;
    if (!adr->num_lvl) {
        strncpy(err, "Number of address levels must be non-zero", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (adr->num_lvl > 7) {
        strncpy(err, "Number of address levels cannot be greater than seven", PCCC_ERR_LEN);
        return PCCC_EPARAM;;
    }
    /*
    * Assemble the mask byte, setting each bit position for levels that
    * are used.
    */
    for (i = adr->num_lvl; i--;) {
        if ((adr->lvl[i] > 999)) {
            strncpy(err, "PLC logical binary address level values must be ess than 1000", PCCC_ERR_LEN);
            return PCCC_EPARAM;
        }
        mask |= 1 << i;
    }
    if (buf_append_byte(dst, mask) || append_lvls(dst, mask, adr->lvl)) {
        strncpy(err, "enc_plc5_lba()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Appends level values for PLC logical binary addresses.
*
* Arguments : dst - Destination buffer.
*             mask - Bitmask indicating which levels to use.
*             lvls - Pointer to array of level values.
*
* Return Value : Zero if successful.
*                Non-zero if the target buffer overflowed.
*/
static int append_lvls(BUF *dst, uint8_t mask, const uint16_t *lvls)
{
    register int i;
    for (i = 0; mask; i++) {
        if ((mask & 1) && addr_encode(dst, lvls[i])) return -1;
        mask >>= 1;
    }
    return 0;
}

/*
* Description : Encodes a PLC logical ASCII address into a buffer.
*
* Arguments : dst - Pointer to the destination buffer. The address will
*                   be appended to the end of the buffer.
*             adr - Pointer to the address to encode.
*             err - Pointer to the location to store a descriptive error
*                   string.
*
* Return Value : PCCC_SUCCESS if the address was encoded successfully.
*                PCCC_EPARAM if the address was too long or empty.
*                PCCC_EOVERFLOW if the target buffer could not hold the
*                               encoded address.
*/
static PCCC_RET_T enc_plc_laa(BUF *dst, const char *adr, char *err)
{
    size_t len = strlen(adr);
    if (len >= PCCC_PLC_LAA_LEN) {
        strncpy(err, "PLC logical ASCII address too long", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (!len) {
        strncpy(err, "PLC logical ASCII address cannot be empty", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    /*
    * The address is prefixed with a two byte sequence, NULL '$'.
    */
    if (buf_append_byte(dst, 0)
        || buf_append_byte(dst, '$')
        || buf_append_str(dst, adr)
        || buf_append_byte(dst, 0)) /* A NULL signfies the end of the address. */
    {
        strncpy(err, "enc_plc5_laa()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

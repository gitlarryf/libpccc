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
* Bit position definitions for boolean members of structured data types.
*/
#define BIT_TMR_EN 0x8000
#define BIT_TMR_TT 0x4000
#define BIT_TMR_DN 0x2000
#define BIT_CNT_CU 0x8000
#define BIT_CNT_CD 0x4000
#define BIT_CNT_DN 0x2000
#define BIT_CNT_OV 0x1000
#define BIT_CNT_UN 0x0800
#define BIT_CNT_UA 0x0400
#define BIT_CTL_EN 0x8000
#define BIT_CTL_EU 0x4000
#define BIT_CTL_DN 0x2000
#define BIT_CTL_EM 0x1000
#define BIT_CTL_ER 0x0800
#define BIT_CTL_UL 0x0400
#define BIT_CTL_IN 0x0200
#define BIT_CTL_FD 0x0100

static int enc_td_param(BUF *dst, uint64_t x, char *err);
static int dec_td_param(BUF *src, uint64_t *x, int bytes);
static PCCC_RET_T enc_int(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_int(BUF *src, void *dest, char *err);
static PCCC_RET_T enc_timer(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_timer(BUF *src, void *dest, char *err);
static PCCC_RET_T enc_counter(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_counter(BUF *src, void *dest, char *err);
static PCCC_RET_T enc_control(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_control(BUF *src, void *dest, char *err);
static PCCC_RET_T enc_float(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_float(BUF *src, void *dest, char *err);
static PCCC_RET_T enc_str(BUF *dest, const void *src, char *err);
static PCCC_RET_T dec_str(BUF *src, void *dest, char *err);

/*
* Description : Encodes an array of data into a command message. The source
*               of the data is the command message's user data. The encoded
*               data is appended to the message's buffer.
*
* Arguments : msg - Message to encode data into. The source data is taken from
*                   the message's 'udata' member and encoded into its
*                   'buf' member.
*             err - Pointer to the location to store a descriptive error
*                   string.
*
* Return Value :
*/
extern PCCC_RET_T data_enc_array(DF1MSG *msg, char *err)
{
    PCCC_RET_T(*encoder)(BUF *, const void *, char *);
    void *udata = msg->udata;
    unsigned int elements = msg->elements;
    switch (msg->file_type) {
        case PCCC_FT_BIN:
        case PCCC_FT_INT:
        case PCCC_FT_STAT:
            encoder = enc_int;
            break;
        case PCCC_FT_TIMER:
            encoder = enc_timer;
            break;
        case PCCC_FT_COUNT:
            encoder = enc_counter;
            break;
        case PCCC_FT_CTL:
            encoder = enc_control;
            break;
        case PCCC_FT_FLOAT:
            encoder = enc_float;
            break;
        case PCCC_FT_STR:
            encoder = enc_str;
            break;
        default:
            strncpy(err, "Unsupported file type.", PCCC_ERR_LEN);
            return -1;
            break;
    }
    while (elements--) {
        PCCC_RET_T ret;
        ret = encoder(msg->buf, udata, err);
        if (ret != PCCC_SUCCESS) return ret;
        (char*)udata += msg->usize;
    }
    return 0;
}

/*
* Description : Decodes an array of data from a received reply. The decoded
*               data is placed into the user data pointed to by the command
*               message structure.
*
* Arguments :
*
* Return Value : PCCC_SUCCESS if successful.
*/
extern PCCC_RET_T data_dec_array(BUF *rply, DF1MSG *msg, char *err)
{
    unsigned int elements = msg->elements;
    PCCC_RET_T(*decoder)(BUF *, void *, char *err);
    void *udata = msg->udata;
    switch (msg->file_type) {
        case PCCC_FT_BIN:
        case PCCC_FT_INT:
        case PCCC_FT_STAT:
            decoder = dec_int;
            break;
        case PCCC_FT_TIMER:
            decoder = dec_timer;
            break;
        case PCCC_FT_COUNT:
            decoder = dec_counter;
            break;
        case PCCC_FT_CTL:
            decoder = dec_control;
            break;
        case PCCC_FT_FLOAT:
            decoder = dec_float;
            break;
        case PCCC_FT_STR:
            decoder = dec_str;
            break;
        default:
            strncpy(err, "Unsupported file type.", PCCC_ERR_LEN);
            return PCCC_EPARAM;
            break;
    }
    while (elements--) {
        PCCC_RET_T ret;
        ret = decoder(rply, udata, err);
        if (ret != PCCC_SUCCESS) return ret;
        (char*)udata += msg->usize;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a type/data parameter into a buffer. The type and size
*               values must fit into a seven byte unsigned integer.
*
* Arguments : dst - Pointer to destination buffer.
*             type - Type value to encode.
*             size - Size value to encode.
*             err - Storage location for error description.
*
* Return Value : PCCC_SUCCESS if encoded successfully.
*                PCCC_EPARAM if the type or size arguments were invalid.
*                PCCC_EOVERFLOW if the destination buffer ran out of space.
*/
extern PCCC_RET_T data_enc_td(BUF *dst, uint64_t type, uint64_t size, char *err)
{
    int bytes;
    uint8_t *flag;
    uint8_t check = type >> 56;
    /*
    * Make sure the type and size values do not exceed seven bytes.
    */
    if (check) {
        strncpy(err, "Type/data parameter 'Type' value doesn't fit within seven byte limit", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    check = size >> 56;
    if (check) {
        strncpy(err, "Type/data parameter 'Size' value doesn't fit within seven byte limit", PCCC_ERR_LEN);
        return PCCC_EPARAM;
    }
    if (buf_append_byte(dst, 0)) /* Skip over the flag byte for now. */
    {
        strncpy(err, "data_enc_td()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    flag = &dst->data[dst->index - 1];
    /*
    * If the type value is less than 8, place it directly in the the upper
    * nibble of the flag byte.
    */
    if (type < 8) *flag = type << 4;
    else {
        *flag = 0x80; /* Set the extended type bit in the flag. */
        bytes = enc_td_param(dst, type, err);
        if (bytes < 0) return PCCC_EOVERFLOW;
        *flag |= bytes << 4; /* Extended type length in the flag upper nibble. */
    }
    /*
    * If the size value is less than 8, place it directly in the the lower
    * nibble of the flag byte.
    */
    if (size < 8) *flag |= size;
    else {
        *flag |= 0x08; /* Set the extended size bit in the flag. */
        bytes = enc_td_param(dst, size, err);
        if (bytes < 0) return PCCC_EOVERFLOW;
        *flag |= bytes; /* Extended size length in the flag lower nibble. */
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a type/data parameter from a buffer.
*
* Arguments : src - Pointer to the buffer containing the type/data parameter.
*             type - Location to store the type value.
*             size - Location to store the size value.
*             err - Storage location for error description.
*
* Return Value : Zero if successful.
*                Non-zero if the end of the buffer was reached before the
*                entire type/data parameter could be decoded.
*/
extern int data_dec_td(BUF *src, uint64_t *type, uint64_t *size, char *err)
{
    uint8_t flag = 0;
    int ret = buf_get_byte(src, &flag);
    if (flag & 0x80) /* Type value is extended following flag byte. */
        ret |= dec_td_param(src, type, (flag & 0x70) >> 4);
    else *type = (flag & 0x70) >> 4; /* Get type value from upper flag nibble. */
    if (flag & 0x08) /* Size value is extended following type bytes, if any. */
        ret |= dec_td_param(src, size, flag & 0x07);
    else *size = flag & 0x07; /* Get size value from lower flag nibble. */
    if (ret)
        strncpy(err, "Unexpected end of buffer while decoding type/data parameter", PCCC_ERR_LEN);
    return ret;
}

/*
* Description : Encodes a type/data parameter whose value is greather than
*               seven into a multi-byte sequence.
*
* Arguments : dst - Pointer to buffer in which to place encoded value.
*             x - Value to encode.
*             err - Storage location for error description.
*
* Return Value : The number of bytes required to encode the value, up to
*                seven.
*                Less than zero if the end of the buffer was reached before
*                the entire value could be encoded.
*/
static int enc_td_param(BUF *dst, uint64_t x, char *err)
{
    register int i;
    for (i = 0; x; i++) {
        if (buf_append_byte(dst, x & 0x00000000000000ff)) {
            strncpy(err, "enc_td_param()", PCCC_ERR_LEN);
            return -1;
        }
        x >>= 8;
    }
    return i + 1;
}

/*
* Description : Decodes a multi-byte value from a type/data parameter.
*
* Arguments : src - Pointer to the source buffer.
*             x - Pointer to location to store decoded value.
*             bytes - The number of bytes to decode, up to seven.
*
* Return Value : Zero if successfull.
*                Non-zero if the end of the buffer was reached before decoding
*                the specified number of bytes.
*/
static int dec_td_param(BUF *src, uint64_t *x, int bytes)
{
    uint8_t b;
    register int i;
    *x = 0;
    for (i = 0; i < bytes; i++) {
        if (buf_get_byte(src, &b)) return -1;
        *x |= b << i * 8;
    }
    return 0;
}

/*
* Description : Encodes a sixteen bit signed integer into a buffer.
*
* Arguments :
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_int(BUF *dest, const void *src, char *err)
{
    if (buf_append_word(dest, htols(*(uint16_t *)src))) {
        strncpy(err, "enc_int()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a sixteen bit integer from a buffer.
*
* Arguments :
*
* Return Value :
*/
static PCCC_RET_T dec_int(BUF *src, void *dest, char *err)
{
    uint16_t word;
    if (buf_get_word(src, &word)) {
        strncpy(err, "dec_int()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    *(uint16_t *)dest = ltohs(word);
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a timer structure into a six byte sequence.
*
* Arguments : target - Destination buffer.
*             src - Pointer to source timer.
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_timer(BUF *dest, const void *src, char *err)
{
    const PCCC_TIMER_T *tmr = (PCCC_TIMER_T *)src;
    uint16_t bits = 0;
    if (tmr->en) bits |= BIT_TMR_EN;
    if (tmr->tt) bits |= BIT_TMR_TT;
    if (tmr->dn) bits |= BIT_TMR_DN;
    if (tmr->base == PCCC_TB1) bits |= 0x0200;
    if (buf_append_word(dest, htols(bits))
        || buf_append_word(dest, htols(tmr->pre))
        || buf_append_word(dest, htols(tmr->acc))) {
        strncpy(err, "enc_timer()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a timer structure from a six byte sequence.
*
* Arguments : src - Pointer to the source buffer.
*             dest - Pointer to the target timer structure.
*
* Return Value :
*/
static PCCC_RET_T dec_timer(BUF *src, void *dest, char *err)
{
    int overflow;
    PCCC_TIMER_T *tmr = (PCCC_TIMER_T *)dest;
    uint16_t x;
    overflow = buf_get_word(src, &x); /* First word contains the control bits.*/
    x = ltohs(x);
    tmr->en = x & BIT_TMR_EN ? 1 : 0;
    tmr->tt = x & BIT_TMR_TT ? 1 : 0;
    tmr->dn = x & BIT_TMR_DN ? 1 : 0;
    tmr->base = (x & 0x0200) ? PCCC_TB1 : PCCC_TB100;
    overflow |= buf_get_word(src, &x); /* Second word is the preset. */
    tmr->pre = ltohs(x);
    overflow |= buf_get_word(src, &x); /* Third word is the accumulator. */
    tmr->acc = ltohs(x);
    if (overflow) {
        strncpy(err, "dec_timer()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a counter structure into a 6 byte sequence.
*
* Arguments : dest - Pointer to the target buffer.
*             src - Pointer to the source counter structure.
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_counter(BUF *dest, const void *src, char *err)
{
    const PCCC_COUNT_T *cnt = (PCCC_COUNT_T *)src;
    uint16_t bits = 0;
    if (cnt->cu) bits |= BIT_CNT_CU;
    if (cnt->cd) bits |= BIT_CNT_CD;
    if (cnt->dn) bits |= BIT_CNT_DN;
    if (cnt->ov) bits |= BIT_CNT_OV;
    if (cnt->un) bits |= BIT_CNT_UN;
    if (cnt->ua) bits |= BIT_CNT_UA;
    if (buf_append_word(dest, htols(bits))
        || buf_append_word(dest, htols(cnt->pre))
        || buf_append_word(dest, htols(cnt->acc))) {
        strncpy(err, "enc_counter()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a six byte counter sequence.
*
* Arguments : src - Pointer to the source buffer.
*             dest - Pointer to the target counter structure.
*
* Return Value :
*/
static PCCC_RET_T dec_counter(BUF *src, void *dest, char *err)
{
    PCCC_COUNT_T *cnt = (PCCC_COUNT_T *)dest;
    uint16_t x;
    int overflow = buf_get_word(src, &x);
    x = ltohs(x);
    cnt->cu = x & BIT_CNT_CU ? 1 : 0;
    cnt->cd = x & BIT_CNT_CD ? 1 : 0;
    cnt->dn = x & BIT_CNT_DN ? 1 : 0;
    cnt->ov = x & BIT_CNT_OV ? 1 : 0;
    cnt->un = x & BIT_CNT_UN ? 1 : 0;
    cnt->ua = x & BIT_CNT_UA ? 1 : 0;
    overflow |= buf_get_word(src, &x);
    cnt->pre = ltohs(x);
    overflow |= buf_get_word(src, &x);
    cnt->acc = ltohs(x);
    if (overflow) {
        strncpy(err, "dec_counter()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a control structure into a six byte sequence.
*
* Arguments : dest - Target buffer.
*             src - Pointer to source control structure.
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_control(BUF *dest, const void *src, char *err)
{
    const PCCC_CTL_T *ctl = (PCCC_CTL_T *)src;
    uint16_t bits = 0;
    if (ctl->en) bits |= BIT_CTL_EN;
    if (ctl->eu) bits |= BIT_CTL_EU;
    if (ctl->dn) bits |= BIT_CTL_DN;
    if (ctl->em) bits |= BIT_CTL_EM;
    if (ctl->er) bits |= BIT_CTL_ER;
    if (ctl->ul) bits |= BIT_CTL_UL;
    if (ctl->in) bits |= BIT_CTL_IN;
    if (ctl->fd) bits |= BIT_CTL_FD;
    if (buf_append_word(dest, htols(bits))
        || buf_append_word(dest, htols(ctl->len))
        || buf_append_word(dest, htols(ctl->pos))) {
        strncpy(err, "enc_control()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a six byte sequence into a control structure.
*
* Arguments : src - Pointer to the source buffer.
*             dest - Pointer to the destination control structure.
*
* Return Value :
*/
static PCCC_RET_T dec_control(BUF *src, void *dest, char *err)
{
    PCCC_CTL_T *ctl = (PCCC_CTL_T *)dest;
    uint16_t x;
    int overflow = buf_get_word(src, &x);
    x = ltohs(x);
    ctl->en = x & BIT_CTL_EN ? 1 : 0;
    ctl->eu = x & BIT_CTL_EU ? 1 : 0;
    ctl->dn = x & BIT_CTL_DN ? 1 : 0;
    ctl->em = x & BIT_CTL_EM ? 1 : 0;
    ctl->er = x & BIT_CTL_ER ? 1 : 0;
    ctl->ul = x & BIT_CTL_UL ? 1 : 0;
    ctl->in = x & BIT_CTL_IN ? 1 : 0;
    ctl->fd = x & BIT_CTL_FD ? 1 : 0;
    overflow |= buf_get_word(src, &x);
    ctl->len = ltohs(x);
    overflow |= buf_get_word(src, &x);
    ctl->pos = ltohs(x);
    if (overflow) {
        strncpy(err, "dec_control()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a floating point integer into a buffer.
*
* Arguments :
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_float(BUF *dest, const void *src, char *err)
{
    if (buf_append_long(dest, htoll(*(uint32_t *)src))) {
        strncpy(err, "enc_float()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a floating point number from a buffer.
*
* Arguments :
*
* Return Value :
*/
static PCCC_RET_T dec_float(BUF *src, void *dest, char *err)
{
    uint32_t x;
    PCCC_FLOAT_T *f = (PCCC_FLOAT_T *)dest;
    PCCC_FLOAT_T *p = (PCCC_FLOAT_T *)((void *)&x);
    if (buf_get_long(src, &x)) {
        strncpy(err, "dec_float()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    x = ltohl(x);
    *f = *p;
    return PCCC_SUCCESS;
}

/*
* Description : Encodes a string element into a buffer.
*
* Arguments : dest - Target buffer.
*             src - Pointer to the source string element.
*             err - Storage location for error description.
*
* Return Value : PCCC_SUCCESS if the data was successfully encoded.
*                PCCC_EPARAM if the given string length was invalid.
*                PCCC_EOVERLOW if the target buffer could not hold the
*                              encoded data.
*/
static PCCC_RET_T enc_str(BUF *dest, const void *src, char *err)
{
    PCCC_STR_T *s = (PCCC_STR_T *)src;
    register unsigned int i;
    int overflow;
    if (s->len > 82) {
        snprintf(err, PCCC_ERR_LEN, "String element with invalid length, %u. 82 maximum allowed value", s->len);
        return PCCC_EPARAM;
    }
    /*
    * First two bytes are the length.
    */
    overflow = buf_append_word(dest, htols(s->len));
    /*
    * Add the text, swapping the order of every character pair.
    */
    for (i = 0; i < s->len;) {
        uint8_t c = i & 1 ? s->txt[i - 1] : s->txt[i + 1];
        /*
        * The word containing the last character for odd numbered lengths
        * needs a little special attention. The first byte must be zero,
        * not one past the end of the string, which may be junk if the string
        * wasn't NULL terminated. The next byte needs to be the actual last
        * character of the string.
        */
        if ((++i == s->len) && (i & 1)) {
            overflow |= buf_append_byte(dest, 0);
            overflow |= buf_append_byte(dest, s->txt[i - 1]);
            /*
            * Account for the extra zero byte we added so the correct amount
            * of pad bytes are appended.
            */
            i++;
        } else overflow |= buf_append_byte(dest, c);
    }
    /*
    * Pad the remaining byte(s) with zero(s).
    */
    for (i = 82 - i; i; i--) overflow |= buf_append_byte(dest, 0);
    if (overflow) {
        strncpy(err, "enc_str()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

/*
* Description : Decodes a string element from a buffer.
*
* Arguments :
*
* Return Value :
*/
static PCCC_RET_T dec_str(BUF *src, void *dest, char *err)
{
    uint16_t len;
    register unsigned int i;
    int overflow;
    PCCC_STR_T *s = (PCCC_STR_T *)dest;
    /*
    * The length element is the first sixteen bits.
    */
    overflow = buf_get_word(src, &len);
    s->len = ltohs(len);
    if (s->len > 82) s->len = 82;
    /*
    * The text follows, but is encoded in words of two characters each.
    * In each word, the character that should come first is in the
    * most siginificant byte, so every character pair must be reversed.
    */
    for (i = 0; (i < 82) && !overflow; i++) {
        uint8_t c;
        overflow |= buf_get_byte(src, &c);
        if (i & 1) s->txt[i - 1] = c;
        else s->txt[i + 1] = c;
    }
    s->txt[s->len] = 0; /* Make sure the result is NULL terminated. */
    if (overflow) {
        strncpy(err, "dec_str()", PCCC_ERR_LEN);
        return PCCC_EOVERFLOW;
    }
    return PCCC_SUCCESS;
}

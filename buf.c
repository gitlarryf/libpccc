/*
 * Buffer manipulation functions.
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

#include "common.h"

/*
 * Description : Allocates and initializes a new buffer.
 *               The new buffer will be empty.
 *
 * Arguments : bytes - The size of the buffer to allocate.
 *
 * Return Value : A pointer to the new buffer.
 *                NULL if a memory allocation error occured.
 */
extern BUF *buf_new(size_t bytes)
{
  BUF *b;
  b = (BUF *)malloc(sizeof(BUF) + bytes);
  if (b == NULL) return NULL;
  b->data = (uint8_t *)(b + 1);
  b->max = bytes;
  buf_empty(b);
  return b;
}

/*
 * Description : Appends the data in one buffer to the end of another, making
 *               sure not to overflow the destination.
 *
 * Arguments : dst - Target buffer.
 *             src - Buffer to append.
 *
 * Return Value : Zero if successful.
 *                Non-zero if there is insufficient room in the destination
 *                buffer.
 */
extern int buf_append_buf(BUF *dst, const BUF *src)
{
  size_t new_len = dst->len + src->len;
  if (new_len > dst->max) return -1;
  memcpy((void *)(dst->data + dst->len), (void *)src->data, src->len);
  dst->len = new_len;
  return 0;
}

/*
 * Description : Appends a single byte to the end of a buffer.
 *
 * Arguments : dst - Pointer to destionation buffer.
 *             src - Byte to append.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the buffer would overflow.
 */
extern int buf_append_byte(BUF *dst, uint8_t src)
{
  if (dst->len == dst->max) return -1;
  dst->data[dst->len++] = src;
  return 0;
}

/*
 * Description : Appends a 16 bit word to the end of a buffer.
 *
 * Arguments : dst - Pointer to the target buffer.
 *             src - Word to append.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the buffer would overflow.
 */
extern int buf_append_word(BUF *dst, uint16_t src)
{
  size_t new_len = dst->len + 2;
  if (new_len > dst->max) return -1;
  *(uint16_t *)(dst->data + dst->len) = src;
  dst->len = new_len;
  return 0;
}

/*
 * Description : Appends a 32 bit word to the end of a buffer.
 *
 * Arguments : dst - Pointer to the target buffer.
 *             src - Source word.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the buffer would overflow.
 */
extern int buf_append_long(BUF *dst, uint32_t src)
{
  size_t new_len = dst->len + 4;
  if (new_len > dst->max) return -1;
  *(uint32_t *)(dst->data + dst->len) = src;
  dst->len = new_len;
  return 0;
}

/*
 * Description : Appends a string to the end of a buffer.
 *
 * Arguments : dst - Target buffer.
 *             src - String to append.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the buffer would overflow.
 */
extern int buf_append_str(BUF *dst, const char *src)
{
  size_t slen, nlen;
  slen = strlen(src);
  nlen = slen + dst->len;
  if (nlen > dst->max) return -1;
  memcpy((void *)(dst->data + dst->len), (void *)src, slen);
  dst->len = nlen;
  return 0;
}

/*
 * Description :
 *
 * Arguments :
 *
 * Return Value : Zero if successful.
 *                Non-zero if the buffer would overflow, no data is copied.
 */
extern int buf_append_blob(BUF *dst, void *src, size_t len)
{
  size_t nlen = len + dst->len;
  if (nlen > dst->max) return -1;
  memcpy((void *)(dst->data + dst->len), src, len);
  dst->len = nlen;
  return 0;
}

/*
 * Description : Retrieves a byte at the index from a buffer.
 *
 * Arguments : src - Source buffer.
 *             dst - Location to place the next byte.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the index is already at the end of the buffer.
 */
extern int buf_get_byte(BUF *src, uint8_t *dst)
{
  if (src->len == src->index) return -1;
  *dst = src->data[src->index++];
  return 0;
}

/*
 * Description : Retrieves a 16 bit word at the index from a buffer.
 *
 * Arguments : src - Source buffer.
 *             dst - Location for retrieved word. 
 *
 * Return Value : Zero if successful.
 *                Non-zero if the index is already at the end of the buffer.
 */
extern int buf_get_word(BUF *src, uint16_t *dst)
{
  size_t new_index = src->index + 2;
  if (new_index > src->len) return -1;
  *dst = *(uint16_t *)(src->data + src->index);
  src->index = new_index;
  return 0;
}

/*
 * Description :
 *
 * Arguments :
 *
 * Return Value : Zero if successful.
 *                Non-zero if the index is already at the end of the buffer.
 */
extern int buf_get_long(BUF *src, uint32_t *dst)
{
  size_t new_index = src->index + 4;
  if (new_index > src->len) return -1;
  *dst = *(uint32_t *)(src->data + src->index);
  src->index = new_index;
  return 0;
}

/*
 * Description : Reads data from a file descriptor into a buffer. Buffer
 *               contents are always overwritten.
 *
 * Arguments : fd - Source file descriptor.
 *             buf - Destination buffer.
 *
 * Return Value : Same as read().
 */
extern ssize_t buf_read(int fd, BUF *dst)
{
  ssize_t len;
again:
#ifdef _WIN32
    if (ReadFile((HANDLE)fd, (void*)dst->data, dst->max, &len, NULL)) {
        if (len < 0 && GetLastError() == 0) {
            goto again;
        } else {
            dst->index = 0;
            dst->len = len;
        }
    }
#else
  len = read(fd, (void *)dst->data, dst->max);
  if ((len < 0) && (errno == EINTR)) goto again;
  else
    {
      dst->index = 0;
      dst->len = len;
    }
#endif
  return len;
}

/*
 * Description : Checks to see if a buffer has data ready to be written.
 *
 * Arguments : buf - Pointer to buffer to test.
 *
 * Return Value : Nonzero if the buffer has data ready to be written.
 *                Zero if the buffer is empty or has been completely written.
 */
extern int buf_write_ready(const BUF *buf)
{
  return (buf->len && (buf->index != buf->len)) ? 1 : 0;
}

/*
 * Description : Writes the contents of a buffer to a file descriptor.
 *
 * Arguments : fd - Target file descriptor.
 *             src - Source data buffer.
 *
 * Return Value : Same as write().
 */
extern ssize_t buf_write(int fd, BUF *src)
{
  ssize_t bytes;
  size_t len = src->len - src->index;
again:
#ifdef _WIN32
    if (WriteFile((HANDLE)fd, (void*)(src->data + src->index), len, &bytes, NULL)) {
        if (bytes < 0) {
            if (GetLastError() > 0) {
                return -1;
            } else {
                goto again;
            }
        }
    }
#else
  bytes = write(fd, (void *)(src->data + src->index), len);
  if (bytes < 0)
    {
      if (errno == EINTR) goto again;
      return -1;
    }
#endif
  src->index += bytes;
  if (src->index == src->len) /* All buffer contents written .*/
    buf_empty(src);
  return bytes;
}

/*
 * Description : Empties a buffer.
 *
 * Arguments : buf - Target buffer.
 *
 * Return Value : None.
 */
extern void buf_empty(BUF *buf)
{
  buf->len = 0;
  buf->index = 0;
  return;
}

/*
 * Description : Free's memory allocated for a buffer.
 *
 * Arguments : buf - Pointer to buffer to free.
 *
 * Return Value : None.
 */
extern void buf_free(BUF *buf)
{
  free((void *)buf);
  return;
}

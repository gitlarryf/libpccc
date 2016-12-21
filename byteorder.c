/*
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
 * Description : Converts a 16 bit word from link to host byte order.
 *
 * Arguments : linkshort - The 16 bit word in link byte order.
 *
 * Return Value : The 16 bit word in host byte order.
 */
extern uint16_t ltohs(uint16_t linkshort)
{
#if BYTE_ORDER == BIG_ENDIAN
  return *(uint8_t *)&linkshort + (*((uint8_t *)&linkshort + 1) << 8);
#else
  return linkshort;
#endif
}

/*
 * Description : Converts a 16 bit word from host to link byte order.
 *
 * Arguments : hostshort - The 16 bit word in host byte order.
 *
 * Return Value : The 16 bit word in link byte order.
 */
extern uint16_t htols(uint16_t hostshort)
{
#if BYTE_ORDER == BIG_ENDIAN
  return (*(uint8_t *)&hostshort << 8) + *((uint8_t *)&hostshort + 1);
#else
  return hostshort;
#endif
}

/*
 * Description : Converts a 32 bit word from link to host byte order.
 *
 * Arguments : linklong - The 32 bit word in link byte order.
 *
 * Return Value : The 32 bit word in host byte order.
 */
extern uint32_t ltohl(uint32_t linklong)
{
#if BYTE_ORDER == BIG_ENDIAN
  return *(uint8_t *)&linklong + (*((uint8_t *)&linklong + 1) << 8) +
    (*((uint8_t *)&linklong + 2) << 16) + (*((uint8_t *)&linklong + 3) << 24);
#else
  return linklong;
#endif
}

/*
 * Description : Converts a 32 bit word from host to link byte order.
 *
 * Arguments : hostlong - The 32 bit word in host byte order.
 *
 * Return Value : The 32 bit word in link byte order.
 */
extern uint32_t htoll(uint32_t hostlong)
{
#if BYTE_ORDER == BIG_ENDIAN
  uint32_t x = htonl(hostlong);
  return (*(uint8_t *)&x << 24) + (*((uint8_t *)&x + 1) << 16) +
    (*((uint8_t *)&x + 2) << 8) + *((uint8_t *)&x + 3);
#else
  return hostlong;
#endif
}

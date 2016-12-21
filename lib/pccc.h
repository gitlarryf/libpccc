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

#ifndef _PCCC_H
#define _PCCC_H

#include <stdint.h>
#include "..\common.h"

/** \file pccc.h */

/**
\mainpage
libpccc is a shared library used to initiate Allen Bradley PCCC commands.
PCCC messages, which can contain either commands or replies, are sent to and
received from a link layer service, which provides the actual connection
to some type of interconnect, DF1 for example. A PCCC connection can only
be used with one link layer connection. However a process may use multiple
PCCC connections at once. A TCP/IP socket is used to connect to the link layer
serivce.

- \subpage conn_mgmt "Connection setup and management"
- \subpage cmd_init "Sending PCCC commands"
- \subpage udata "Controller data types"
*/

#ifdef _WIN32
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

/**
Maximum length of the client name used to register with the link layer server.
Does not include the NULL terminiation.
*/
#define PCCC_NAME_LEN 16

/**
Function return values.

PCCC_ECMD_XXXX values apply only to \ref cmd_init
"functions for sending PCCC commands".

\sa pccc_errstr()
*/
typedef enum
  {
    PCCC_SUCCESS,       //!< No error.
    PCCC_WREADY,        //!< No error, only used in pccc_write_ready().
    PCCC_ENOCON,        //!< The supplied connection pointer was invalid(NULL).
    PCCC_ELINK,         //!< An error occured with the connection to the link layer.
    PCCC_EPARAM,        //!< One of the supplied parameters was invalid.
    PCCC_EFATAL,        //!< A fatal error occured. The connection must be closed.
    PCCC_EOVERFLOW,     //!< An internal buffer overlow occured.
    PCCC_ECMD_NOBUF,    //!< No message buffers were available to process command.
    PCCC_ECMD_NODELIVER,//!< Link layer could not deliver command.
    PCCC_ECMD_TIMEOUT,  //!< Command timed out awaiting a reply.
    PCCC_ECMD_REPLY     //!< Reply contained an error.
  } PCCC_RET_T;

/**
Structure allocated by pccc_new(). Passed as a pointer to all other functions
to specify which connection to act upon.

Typedef'ed as PCCC.
*/
struct pccc 
{
  int fd;               //!< Link layer service connection descriptor.
  uint8_t src_addr;     //!< Source node address.
  unsigned int timeout; //!< Command timeout in seconds.
  void *priv_data;
};

typedef struct pccc PCCC;

typedef void (* UFUNC)(PCCC *, PCCC_RET_T, void *);

/**
\page udata Controller data types

These are data types that can be read from and/or written to controllers with
certian \ref cmd_init "PCCC commands". The 'udata' argument is used to point
to an array of one of these types:

Data table element types:
- \ref PCCC_INT_T "PCCC_INT_T" Integer
- \ref PCCC_BIN_T "PCCC_BIN_T" Binary
- \ref PCCC_FLOAT_T "PCCC_FLOAT_T" Float
- \link pccc_timer_t PCCC_TIMER_T \endlink Timer
- \link pccc_count_t PCCC_COUNT_T \endlink Counter
- \link pccc_ctl_t PCCC_CTL_T \endlink Control
- \link pccc_str_t PCCC_STR_T \endlink String
- \ref PCCC_STAT_T "PCCC_STAT_T" Status

Data table information:
- \link pccc_slc_fi_t PCCC_SLC_FI_T \endlink

The following constants are #define'd to the number of bytes that each data
type requires per element to transfer. This is *NOT* the same as the number
of bytes required to store an element on the host machine(sizeof()).
These constants can be used to help calculate the maximum amount of elements
that can be transferred using various \ref cmd_init "PCCC commands".
- \ref PCCC_SO_INT "PCCC_SO_INT"
- \ref PCCC_SO_BIN "PCCC_SO_BIN"
- \ref PCCC_SO_FLOAT "PCCC_SO_FLOAT"
- \ref PCCC_SO_TIMER "PCCC_SO_TIMER"
- \ref PCCC_SO_COUNT "PCCC_SO_COUNT"
- \ref PCCC_SO_CTL "PCCC_SO_CTL"
- \ref PCCC_SO_STR "PCCC_SO_STR"
- \ref PCCC_SO_STAT "PCCC_SO_STAT"
*/

/**
Controller file types. These are used to specify what type of data is being
transferred.
*/
typedef enum
  {
    PCCC_FT_STAT,   //!< Status
    PCCC_FT_BIN,    //!< Binary
    PCCC_FT_TIMER,  //!< Timer
    PCCC_FT_COUNT,  //!< Counter
    PCCC_FT_CTL,    //!< Control
    PCCC_FT_INT,    //!< Integer
    PCCC_FT_FLOAT,  //!< Floating point
    PCCC_FT_OUT,    //!< Output
    PCCC_FT_IN,     //!< Input
    PCCC_FT_STR,    //!< String
    PCCC_FT_ASC,    //!< ASCII
    PCCC_FT_BCD     //!< Binary coded decimal
  } PCCC_FT_T;

/**
A sixteen bit signed integer. Typically stored in 'N' type data files.
\ref PCCC_SO_INT "PCCC_SO_INT" bytes per element.
*/
typedef int16_t PCCC_INT_T;

/**
The number of bytes required to transmit an \ref PCCC_INT_T "integer"
element.
*/
#define PCCC_SO_INT 2

/**
A sixteen bit unsigned integer. Usually stored in 'B' type data files.
\ref PCCC_SO_BIN "PCCC_SO_BIN" bytes per element.
*/
typedef uint16_t PCCC_BIN_T;

/**
The number of bytes required to transmit a \ref PCCC_BIN_T "binary"
element.
*/
#define PCCC_SO_BIN 2

/**
Usually stored in 'F' type data files.
\ref PCCC_SO_FLOAT "PCCC_SO_FLOAT" bytes per element.
*/
typedef float PCCC_FLOAT_T;

/**
The number of bytes required to transmit a \ref PCCC_FLOAT_T "floating point"
element.
*/
#define PCCC_SO_FLOAT 4

/**
Timer time base values.

\sa pccc_timer_t
*/
typedef enum
{
  PCCC_TB1, //!< Seconds
  PCCC_TB100, //!< 1/100 seconds
} PCCC_TBASE_T;

/**
A timer structure. Typically stored in 'T' type data files.
\ref PCCC_SO_TIMER "PCCC_SO_TIMER" bytes per element.

\sa PCCC_TBASE_T

Typedef'ed as PCCC_TIMER_T.
*/
struct pccc_timer_t
{
  PCCC_INT_T pre; //!< Preset
  PCCC_INT_T acc; //!< Accumulator
  PCCC_TBASE_T base; //!< Time base. PCCC_TB1 or PCCC_TB100
  unsigned en : 1; //!< Enabled bit
  unsigned tt : 1; //!< Timing bit
  unsigned dn : 1; //!< Done bit
};

typedef struct pccc_timer_t PCCC_TIMER_T;

/**
The number of bytes required to transmit a \link pccc_timer_t timer \endlink
element.
*/
#define PCCC_SO_TIMER 6

/**
A counter structure. Typically stored in 'C' type data files.
\ref PCCC_SO_COUNT "PCCC_SO_COUNT" bytes per element.

Typedef'ed as PCCC_COUNT_T.
 */
struct pccc_count_t
{
  PCCC_INT_T pre;   //!< Preset
  PCCC_INT_T acc;   //!< Accumulator
  unsigned cu : 1;  //!< Count up enable
  unsigned cd : 1;  //!< Count down enable
  unsigned dn : 1;  //!< Done
  unsigned ov : 1;  //!< Count up overflow
  unsigned un : 1;  //!< Count down underflow
  unsigned ua : 1;  //!< Update accumulator
};

typedef struct pccc_count_t PCCC_COUNT_T;

/**
The number of bytes required to transmit a \link pccc_count_t counter \endlink
element.
*/
#define PCCC_SO_COUNT 6

/**
A countrol structure. Typically stored in 'R' type data files.
\ref PCCC_SO_CTL "PCCC_SO_CTL" bytes per element.

Typedef'ed as PCCC_CTL_T.
*/
struct pccc_ctl_t
{
  PCCC_INT_T pos;   //!< Position
  PCCC_INT_T len;   //!< Length
  unsigned en : 1;  //!< Enable
  unsigned eu : 1;  //!< Enable unload
  unsigned dn : 1;  //!< Done
  unsigned em : 1;  //!< Empty
  unsigned er : 1;  //!< Error
  unsigned ul : 1;  //!< ul bit
  unsigned in : 1;  //!< Inhibit
  unsigned fd : 1;  //!< Found
};

typedef struct pccc_ctl_t PCCC_CTL_T;

/**
The number of bytes required to transmit a \link pccc_ctl_t control \endlink
element.
*/
#define PCCC_SO_CTL 6

/**
A string structure, typically stored in 'ST' type data files. When writing
string elements, the text doesn't need to be NULL terminated. Any characters
beyond the length specified by the len member will be replaced with pad(zero)
bytes. The len member must be set by the user program, the library will not
set this based on the contents of the txt member. All bytes, including
premature NULLs will be transmitted up to the last character as specified by
the len member.

When reading string elements, the library will always provide a NULL
termination after the last character as specified by the len member.

\ref PCCC_SO_STR "PCCC_SO_STR" bytes per element.

Typedef'ed as PCCC_STR_T.
*/
struct pccc_str_t
{
  size_t len;   //!< Length. Does not include optional NULL termination.
  char txt[83]; //!< Text. Maximum 82 characters.
};

typedef struct pccc_str_t PCCC_STR_T;

/**
The number of bytes required to transmit a \link pccc_str_t string \endlink
element.
*/
#define PCCC_SO_STR 84

/**
A sixteen bit unsigned integer. Typically stored in 'S' type data files.
When handling status elements, this data type is just a raw 16 bit element,
it is not broken out into individual members as seen when using the
'Structured' radix.

\ref PCCC_SO_STAT "PCCC_SO_STAT" bytes per element.
*/
typedef uint16_t PCCC_STAT_T;

/**
The number of bytes required to transmit a \ref PCCC_STAT_T "status"
element.
*/
#define PCCC_SO_STAT 2

/**
Processor modes. A processor mode change command may only support a subset of
these.
*/
typedef enum
  {
    PCCC_MODE_PROG,         //!< Program
    PCCC_MODE_RUN,          //!< Run
    PCCC_MODE_TEST_CONT,    //!< Test-Cont can
    PCCC_MODE_TEST_SINGLE,  //!< Test-Single scan
    PCCC_MODE_TEST_DEBUG,   //!< Test-Debug single step
    PCCC_MODE_REM_TEST,     //!< Remote test
    PCCC_MODE_REM_RUN       //!< Remote run
  } PCCC_MODE_T;

/**
Structure that holds information describing a SLC data file.

\sa pccc_cmd_ReadSLCFileInfo

Typedef'ed as PCCC_SLC_FI_T.
*/
struct pccc_slc_fi_t
{
  size_t bytes;     //!< Size of the file in bytes.
  size_t elements;  //!< Number of elements in the file.
  PCCC_FT_T type;   //!< Type of file. One of the PCCC_FT_T enumerations.
};

typedef struct pccc_slc_fi_t PCCC_SLC_FI_T;

/**
Node types. These identify what type of module/processor a node is.
*/
typedef enum
  {
    PCCC_NT_PLC3,       //!< Generic PLC-3
    PCCC_NT_PLC5,       //!< Generic PLC-5
    PCCC_NT_PLC_5250    //!< PLC-5/250
  } PCCC_NODE_T;

/**
A PLC logical binary address.

PLC-3's support up to six address levels:
- Level 1 : Data table area
- Level 2 : Context
- Level 3 : Section
- Level 4 : File
- Level 5 : Structure
- Level 6 : Word

PLC-5's support up to four address levels:
- Level 1 : Section. Zero for data table access.
- Level 2 : File
- Level 3 : Element or word
- Level 4 : Sub-element

PLC-5/250's support up to seven address levels.

Typedef'ed as PCCC_PLC_LBA.
*/
struct pccc_plc_lba
{
  size_t num_lvl;   //!< Number of address levels, 1-7.
  uint16_t lvl[7];  //!< Address level values, 0-999.
};

typedef struct pccc_plc_lba PCCC_PLC_LBA;

/**
PLC logical address types.
*/
typedef enum
  {
    PCCC_PLC_ADDR_BIN,  //!< Logical binary address
    PCCC_PLC_ADDR_ASCII //!< ASCII, or symbolic address 
  } PCCC_PLC_ADDR_T;

/**
The maximum allowed length of a PLC logical ASCII address, including the
required NULL termination.
*/
#define PCCC_PLC_LAA_LEN 16

/**
A PLC logical address. The address may be in the form of a logical
binary or a logical ASCII address. Only one address type may be used at a time.
To specify the type of address, set the
'type' element to one of the \ref PCCC_PLC_ADDR_T address type enumerations.

Logical binary addresses require the address levels to be individually
specified as numeric values. The logical ASCII address allows using the same
text notation used when programming PLCs, such as 'N7:0'. When using logical
ASCII addressing, do not include the '$' prefix, it is provided automatically
by the library.

Additional information on PLC addressing for the following products can be found as follows:
- PLC-2 Allen Bradley Publication 5000-6.4.6
- PLC-3 Allen Bradley Publication 5000-6.4.5
- PLC-5 Allen Bradley Publication 5000-6.4.4
- PLC-5/250 Allen Bradley Publication 5000-6.4.3

Typedef'ed as PCCC_PLC_ADDR.
*/
struct pccc_plc_addr
{
  PCCC_PLC_ADDR_T type;             //!< Type of address.
  union
  {
    PCCC_PLC_LBA lba;               //!< Logical binary address.
    char ascii[PCCC_PLC_LAA_LEN];   //!< Logical ASCII address, must be NULL terminated.
  } addr;
};

typedef struct pccc_plc_addr PCCC_PLC_ADDR;

#define PCCC_SE_TMR_BITS 0  /* Control bits */
#define PCCC_SE_TMR_PRE 1   /* Preset */
#define PCCC_SE_TMR_ACC 2   /* Accumulator */

#define PCCC_SE_CNT_BITS 0  /* Control bits */
#define PCCC_SE_CNT_PRE 1   /* Preset */
#define PCCC_SE_CNT_ACC 2   /* Accumulator */

#define PCCC_SE_CTL_BITS 0  /* Control bits */
#define PCCC_SE_CTL_LEN 1   /* Length */
#define PCCC_SE_CTL_POS 2   /* Position */

/*
 * Housekeeping functions.
 */
extern PCCC *pccc_new(uint8_t src_addr, unsigned int timeout, size_t msgs);
extern PCCC_RET_T pccc_connect(PCCC *con, const char *link_host, in_port_t link_port, const char *client_name);
extern PCCC_RET_T pccc_read(PCCC *con);
extern PCCC_RET_T pccc_write_ready(const PCCC *con);
extern PCCC_RET_T pccc_write(PCCC *con);
extern PCCC_RET_T pccc_tick(PCCC *con);
extern PCCC_RET_T pccc_close(PCCC *con);
extern void pccc_free(PCCC *con);
extern void pccc_errstr(PCCC *con, PCCC_RET_T err, char *buf, size_t len);

/*
 * Functions to send PCCC commands.
 */
extern PCCC_RET_T pccc_cmd_Echo(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, size_t bytes);
extern PCCC_RET_T pccc_cmd_SetVariables(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t cycles, uint8_t naks, uint8_t acks);
extern PCCC_RET_T pccc_cmd_SetTimeout(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t cycles);
extern PCCC_RET_T pccc_cmd_SetNAKs(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t naks);
extern PCCC_RET_T pccc_cmd_SetENQs(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t enqs);
extern PCCC_RET_T pccc_cmd_ReadLinkParam(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t *udata);
extern PCCC_RET_T pccc_cmd_SetLinkParam(PCCC *con, UFUNC notify, uint8_t dnode, uint8_t max);
extern PCCC_RET_T pccc_cmd_ChangeModeMicroLogix1000(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode);
extern PCCC_RET_T pccc_cmd_ChangeModeSLC500(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode);
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalRead3AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, PCCC_FT_T file_type, uint16_t file, uint16_t element, uint16_t sub_element, size_t num_elements);
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalRead2AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, PCCC_FT_T file_type, uint16_t file, uint16_t element, size_t num_elements);
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWrite3AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, PCCC_FT_T file_type, uint16_t file, uint16_t element, uint16_t sub_element, size_t num_elements);
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWrite2AddressFields(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, PCCC_FT_T file_type, uint16_t file, uint16_t element, size_t num_elements);
extern PCCC_RET_T pccc_cmd_SetCPUMode(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_MODE_T mode);
extern PCCC_RET_T pccc_cmd_ReadSLCFileInfo(PCCC *con, UFUNC notify, uint8_t dnode, PCCC_SLC_FI_T *udata, uint8_t file_num);
extern PCCC_RET_T pccc_cmd_DisableForces(PCCC *con, UFUNC notify, uint8_t dnode);
extern PCCC_RET_T pccc_cmd_ProtectedTypedLogicalWriteWithMask(PCCC *con, UFUNC notify, uint8_t dnode, void *udata, PCCC_BIN_T mask, PCCC_FT_T file_type, uint16_t file, uint16_t element, uint16_t sub_element, size_t num_elements);
extern PCCC_RET_T pccc_cmd_ReadModifyWrite(PCCC *con, UFUNC notify, uint8_t dnode, const PCCC_PLC_ADDR *addr, const uint16_t *and, const uint16_t *or, size_t sets);
extern PCCC_RET_T pccc_cmd_BitWrite(PCCC *con, UFUNC notify, uint8_t dnode, const PCCC_PLC_ADDR *addr, uint16_t set, uint16_t reset);

#endif /* _PCCC_H */

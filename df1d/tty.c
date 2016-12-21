/*
 * This file is part of df1d.
 * Allen Bradley DF1 link layer service.
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

#include "df1.h"

#define TTY_BUF_SIZE 512

static int alloc_bufs(CONN *conn);
static int set_topts(const CONN *conn, int rate);

/*
 * Description : Opens and configures a serial port.
 *
 * Arguments : conn - Connection pointer.
 *             dev - TTY device to use.
 *             rate - Serial port baud rate.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured.
 */
extern int tty_open(CONN *conn, const char *dev, int rate)
{
  char rate_s[6];
  if (alloc_bufs(conn)) return -1;
#ifdef _WIN32
    conn->tty_fd = (int)CreateFile(dev, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (conn->tty_fd == INVALID_HANDLE_VALUE) {
        log_msg(LOG_ERR, "%s:%d [%s] Error opening TTY device : %s\n", __FILE__, __LINE__, conn->name, strerror(errno));
        return -1;
    }
#else
  conn->tty_fd = open(dev, O_FLAGS);
  if (conn->tty_fd < 0)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error opening TTY device : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  if (!isatty(conn->tty_fd)) /* Make sure device is a TTY. */
    {
      log_msg(LOG_ERR, "%s:%d [%s] %s is not a TTY.\n",
	      __FILE__, __LINE__, conn->name, dev);
      close(conn->tty_fd);
      return -1;
    }
  if (set_topts(conn, rate))
    {
      close(conn->tty_fd);
      return -1;
    }
  if (tcflush(conn->tty_fd, TCIOFLUSH)) /* Flush the I/O buffers. */
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error flushing TTY I/O buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      close(conn->tty_fd);
      return -1;
    }
  switch (rate)
    {
    case B110:
      strcpy(rate_s, "110");
      conn->byte_usec = 91000;
      break;
    case B300:
      strcpy(rate_s, "300");
      conn->byte_usec = 34000;
      break;
    case B600:
      strcpy(rate_s, "600");
      conn->byte_usec = 17000;
      break;
    case B1200:
      strcpy(rate_s, "1200");
      conn->byte_usec = 8400;
      break;
    case B2400:
      strcpy(rate_s, "2400");
      conn->byte_usec = 4200;
      break;
    case B9600:
      strcpy(rate_s, "9600");
      conn->byte_usec = 1100;
      break;
    case B19200:
      strcpy(rate_s, "19200");
      conn->byte_usec = 530;
      break;
    case B38400:
      strcpy(rate_s, "38400");
      conn->byte_usec = 270;
      break;
    }
#endif
  log_msg(LOG_DEBUG, "%s:%d [%s] %s initialized at %sbps 8N1.\n", __FILE__,
	  __LINE__, conn->name, dev, rate_s);
  return 0;
}

/*
 * Description : Read's data from a connection's TTY.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero if successful.
 *                Non-zero if read() failed.
 */
extern int tty_read(CONN *conn)
{
  if (buf_read(conn->tty_fd, conn->tty_in) < 0)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error reading from TTY : %s\n", __FILE__,
	      __LINE__, conn->name, strerror(errno));
      return -1;
    }
  log_msg(LOG_DEBUG, "%s:%d [%s] %u byte(s) received from TTY.\n", __FILE__,
	  __LINE__, conn->name, conn->tty_in->len);
  return 0;
}

/*
 * Description : Writes data to a connection's TTY.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero if successful.
 *                Non-zero if write() failed.
 */
extern int tty_write(CONN *conn)
{
  ssize_t len = buf_write(conn->tty_fd, conn->tty_out);
  if (len < 0)
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Error writing to TTY : %s\n", __FILE__, __LINE__,
	      conn->name, strerror(errno));
      return -1;
    }
  log_msg(LOG_DEBUG, "%s:%d [%s] Wrote %u byte(s) to TTY.\n", __FILE__,
	  __LINE__, conn->name, len);
  if (!conn->tty_out->len) tx_data_sent(conn);
  return 0;
}

/*
 * Description : Closes a connection's TTY.
 *
 * Arguments : conn - Connection ponter.
 *
 * Return Value : None.
 */
extern void tty_close(CONN *conn)
{
  log_msg(LOG_DEBUG, "%s:%d [%s] Closing TTY.\n", __FILE__, __LINE__,
	  conn->name);
  buf_free(conn->tty_in);
  buf_free(conn->tty_out);
 again:
  if (close(conn->tty_fd))
    {
      if (errno == EINTR) goto again;
      log_msg(LOG_ERR, "%s:%d [%s] Error closing TTY : %s\n", __FILE__,
	      __LINE__, conn->name, strerror(errno));
    }
  return;
}

/*
 * Description : Sets terminal options.
 *
 * Arguments : conn - Connection pointer.
 *             rate - TTY baud rate.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured.
 */
static int set_topts(const CONN *conn, int rate)
{
#ifdef _WIN32
    DCB dcb;
    GetCommState(conn->tty_fd, &dcb);

    if(rate) {
        dcb.BaudRate = rate;
    }
    dcb.fBinary             = TRUE;
    dcb.fParity             = FALSE;
    dcb.fOutxCtsFlow        = FALSE;
    dcb.fOutxDsrFlow        = FALSE;
    dcb.fDtrControl         = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity     = FALSE;
    dcb.fOutX               = FALSE;
    dcb.fInX                = FALSE;
    dcb.fErrorChar          = FALSE;
    dcb.fNull               = FALSE;
    dcb.fRtsControl         = RTS_CONTROL_ENABLE; // RTS_CONTROL_HANDSHAKE;
    
    COMMPROP prop;
    GetCommProperties(conn->tty_fd, &prop);
    dcb.XoffLim = (uint16_t)prop.dwCurrentRxQueue / 4;  // Shutdown when the buffer is 3/4 full.
    dcb.XonLim = (uint16_t)prop.dwCurrentRxQueue / 4;   // Resume when it's only 1/4 is full.
    dcb.fAbortOnError = FALSE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    
    if (!SetCommState(conn->tty_fd, &dcb)) {
        log_msg(LOG_ERR, "%s:%d [%s] Failed to set TTY baud rate : %s\n", __FILE__, __LINE__, conn->name, strerror(errno));
        return -1;
    }
    SetCommMask(conn->tty_fd, EV_BREAK | EV_DSR | EV_RING | EV_RLSD | EV_ERR);
    
    COMMTIMEOUTS cto;
    cto.ReadIntervalTimeout = 50;
    cto.ReadTotalTimeoutMultiplier = 0;
    cto.ReadTotalTimeoutConstant = 100;
    cto.WriteTotalTimeoutMultiplier = 0;
    cto.WriteTotalTimeoutConstant = 0;
    
    if (!SetCommTimeouts(conn->tty_fd, &cto)) {
        log_msg(LOG_ERR, "%s:%d [%s] Failed to set termial attributes : %s\n", __FILE__, __LINE__, conn->name, strerror(errno));
        return -1;
    }

    if(conn->tty_fd == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD status;
    GetCommModemStatus(conn->tty_fd, &status);
    
    m_DSR = (status & MS_DSR_ON) != 0;
    BOOL wascarrier = m_bCarrier;
    m_bCarrier = (status & MS_RLSD_ON) != 0;
    
    if(wascarrier && !m_bCarrier) {
        m_bCarrierDropped = TRUE;
    }
    
    if(status != m_dwLastModemStatus) {
        SetEvent(m_hLineChangeEvent);
        m_dwLastModemStatus = status;
    }
#else
  struct termios topts;
  if (tcgetattr(conn->tty_fd, &topts))
    {
      log_msg(LOG_ERR,
	      "%s:%d [%s] Failed to retrieve terminal attributes : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  if (cfsetspeed(&topts, rate))
    {
      log_msg(LOG_ERR, "%s:%d [%s] Failed to set TTY baud rate : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  topts.c_cflag &= ~PARENB; /* No parity. */
  topts.c_cflag &= ~CSIZE; /* 8 bits per byte. */
  topts.c_cflag |= CS8;
  topts.c_cflag &= ~CSTOPB; /* 1 stop bit. */
  topts.c_lflag &= ~ICANON; /* Disable canonical input mode. */
  topts.c_lflag &= ~ECHO; /* Disable echo. */
  topts.c_lflag &= ~ECHOE; /* Disable erase character echo. */
  topts.c_lflag &= ~ISIG; /* Disable signals */
  topts.c_iflag &= ~IXON; /* Disable output flow control. */
  topts.c_iflag &= ~IXOFF; /* Disable input flow control. */
  topts.c_iflag &= ~IXANY; /* Disable output restart characters. */
  topts.c_iflag &= ~ICRNL; /* Disable carrage return remapping. */
  topts.c_iflag &= ~INLCR; /* Disable newline remapping. */
  topts.c_oflag &= ~OPOST; /* Disable output postprocessing. */
  /*
   * Set the minimum bytes and timeout period for the read function.
   */
  topts.c_cc[VMIN] = 1;
  topts.c_cc[VTIME] = 0;
 again:
  if (tcsetattr(conn->tty_fd, TCSANOW, &topts))
    {
      if (errno == EINTR) goto again;
      log_msg(LOG_ERR, "%s:%d [%s] Failed to set termial attributes : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
#endif
  return 0;
}

/*
 * Description : Allocates buffers for raw TTY I/O.
 *
 * Arguments : conn - Connection pointer.
 *
 * Return Value : Zero upon success.
 *                Non-zero if a memory allocation error occured.
 */
static int alloc_bufs(CONN *conn)
{
  conn->tty_in = buf_new(TTY_BUF_SIZE);
  if (conn->tty_in == NULL)
    {
      log_msg(LOG_ERR, "%s:%d [%s] Error allocating TTY buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  conn->tty_out = buf_new(TTY_BUF_SIZE);
  if (conn->tty_out == NULL)
    {
      buf_free(conn->tty_in);
      log_msg(LOG_ERR, "%s:%d [%s] Error allocating TTY buffers : %s\n",
	      __FILE__, __LINE__, conn->name, strerror(errno));
      return -1;
    }
  return 0;
}

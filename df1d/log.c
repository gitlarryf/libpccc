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

#define LOG_ID "df1d"
#define LOG_FACILITY LOG_DAEMON

static int use_syslog;
static int log_mask;

/*
 * Description : Initializes error logging.
 *
 * Arguments : log_to_console - Set to non-zero to print messages
 *                              to stderr.
 *             lev - Highest level message to log.
 *
 * Return Value : None.
 */
extern void log_open(int log_to_console, int lev)
{
#ifdef _WIN32
  use_syslog = log_to_console ? 0 : 1;
#else
  if (!log_to_console) openlog(LOG_ID, LOG_PID, LOG_FACILITY);
  use_syslog = log_to_console ? 0 : 1;
  log_mask = LOG_UPTO(lev);
  setlogmask(log_mask);
  return;
#endif
}

/*
 * Description : Logs a message.
 *
 * Arguments : lev - Message level.
 *             fmt - Message format.
 *
 * Return Value : None.
 */
extern void log_msg(int lev, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  if (use_syslog) {
#ifdef _WIN32
      vfprintf(stderr, fmt, args);
#else
      vsyslog(lev, fmt, args);
#endif
  } else if (LOG_MASK(lev) & log_mask) {
      vfprintf(stderr, fmt, args);
  }
  va_end(args);
  return;
}

/*
 * Description : Closes error logging.
 *
 * Arguments : None.
 *
 * Return Value : None.
 */
extern void log_close(void)
{
#ifndef _WIN32
  closelog();
#endif
  return;
}

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

/*
 * Description : Starts the interval timer.
 *
 * Arguments : None.
 *
 * Return Value : Zero if successful.
 *                Non-zero if setitimer() failed.
 */
extern int timer_start(void)
{
  struct itimerval timer;
  log_msg(LOG_DEBUG, "%s:%d Setting interval timer.\n", __FILE__,
	  __LINE__);
  timer.it_interval.tv_sec = TICK_SEC;
  timer.it_interval.tv_usec = TICK_USEC;
  timer.it_value.tv_sec = TICK_SEC;
  timer.it_value.tv_usec = TICK_USEC;
  if (setitimer(ITIMER_REAL, &timer, NULL))
    {
      log_msg(LOG_ERR, "%s:%d Error setting interval timer : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  return 0;
}

/*
 * Description : Stops the interval timer.
 *
 * Arguments : None.
 *
 * Return Value : Zero if successful.
 *                Non-zero if setitimer() failed.
 */
extern int timer_stop(void)
{
  struct itimerval timer;
  log_msg(LOG_DEBUG, "%s:%d Stopping interval timer.\n", __FILE__,
	  __LINE__);
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 0;
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 0;
  if (setitimer(ITIMER_REAL, &timer, NULL))
    {
      log_msg(LOG_ERR, "%s:%d Error stopping interval timer : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  return 0;
}

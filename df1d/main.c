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

#define VER_MAJOR "1"
#define VER_MINOR "1"

/*
 * Option bits set by command line arguments.
 */
#define OPT_FOREGROUND 1
#define OPT_DEBUG 2

static int cl_args(int argc, char * const argv[], char *cfg_file, int *opts);
static int become_daemon(void);
static int comm_loop(void);
static int set_signals(void);
static void sig_alrm(int signo);
static void sig_term(int signo);
static void sig_hup(int signo);
static void sig_int(int signo);
static void sig_segv(int signo);

static volatile int timeout; /* Set by SIGARLM. */
static volatile int terminate; /* Set by SIGTERM. */
static volatile int restart; /* Set by SIGHUP. */
static volatile int terminate_int; /* Set by SIGINT. */

/*
 * Description : Main program entry point.
 *
 * Arguments : argc - Number of arguments.
 *             argv - Argument vector.
 *
 * Return Value - 
 */
int main(int argc, char *argv[])
{
  int opts = 0; /* Option mask from command line arguments. */
  int loop = 0; /* Zero if program should terminate after comm loop. */
  char cfg_file[PATH_MAX];
  if (cl_args(argc, argv, cfg_file, &opts)) exit(0);
  log_open((opts & OPT_FOREGROUND),
	   (opts & OPT_DEBUG) ? LOG_DEBUG : LOG_INFO);
  log_msg(LOG_INFO, "Starting DF1 link layer service v%s.%s\n", VER_MAJOR,
	  VER_MINOR);
  if (cfg_read(cfg_file))
    {
      log_close();
      exit(0);
    }
  if (!(opts & OPT_FOREGROUND) && become_daemon())
    {
      conn_close_all();
      log_close();
      exit(0);
    }
  if (set_signals())
    {
      conn_close_all();
      log_close();
      exit(0);
    }
  do
    {
      if (timer_start()) break;
      loop = comm_loop();
      conn_close_all();
      if (timer_stop()) break;
      if (loop && cfg_read(cfg_file)) break;
    } while (loop);
  conn_close_all();
  log_close();
  return 0;
}

/*
 * Description : Parses command line arguments.
 *
 * Arguments : argc - Argument count from main().
 *             argv - Argument vector from main().
 *             cfg_file - Location to store config file name.
 *             opts - Option bit mask.
 *
 * Return Value : Zero if successful.
 *                Non-zero if the program should terminate.
 */
static int cl_args(int argc, char * const argv[], char *cfg_file, int *opts)
{
  int i;
  const char cl_opts[] = "dfhv";
  const char usage[] = \
    "Usage: df1d [options] <config file>\n"
    "   -d : Enable debug log messages.\n"
    "   -f : Run in foreground, log to standard error.\n"
    "   -h : Print this message and exit.\n"
    "   -v : Output version information and exit.\n";
  while ((i = getopt(argc, argv, cl_opts)) != -1)
    {
      switch (i)
	{
	case 'd':
	  *opts |= OPT_DEBUG;
	  break;
	case 'f':
	  *opts |= OPT_FOREGROUND;
	  break;
	case 'h':
	  printf("%s", usage);
	  return -1;
	  break;
	case 'v':
	  printf("df1d version %s.%s\n", VER_MAJOR, VER_MINOR);
	  return -1;
	  break;
	case '?':
	  return -1;
	  break;
	}
    }
  if (optind < argc) /* Get configuration file name after options. */
    {    
      strncpy(cfg_file, argv[optind], PATH_MAX);
      return 0;
    }
  else fprintf(stderr, "No configuration file specified.\n%s", usage);
  return -1;
}

/*
 * Description : Converts the process into background.
 *
 * Arguments : None.
 *
 * Return Value : Zero upon success.
 *                Non-zero if an error occured.
 */
static int become_daemon(void)
{
  pid_t pid;
  log_msg(LOG_DEBUG, "%s:%d Becoming background process.\n",
	  __FILE__, __LINE__);
  pid = fork();
  if (pid < 0)
    {
      log_msg(LOG_ERR, "%s:%d Failed to fork daemon process : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  else if (pid != 0) /* Parent terminates. */
    _exit(0);
  if (setsid() < 0) /* Create new session. */
    {
      log_msg(LOG_ERR, "%s:%d Failed to create new session : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  if (chdir("/")) /* Change working directory to root. */
    {
      log_msg(LOG_ERR, "%s:%d Failed to change working directory : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  /*
   * Redirect standard I/O to /dev/null.
   */
  if ((freopen("/dev/null", "r", stdin) == NULL)
      || (freopen("/dev/null", "w", stdout) == NULL)
      || (freopen("/dev/null", "w", stderr) == NULL))
    {
      log_msg(LOG_ERR, "%s:%d Failed to redirect standard I/O : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  return 0;
}

/*
 * Description : Main communication loop.
 *
 * Arguments : None.
 *
 * Return Value : Zero if the program should terminate.
 *                Non-zero if it should restart.
 */
static int comm_loop(void)
{
  sigset_t block_set;
  sigset_t empty_set;
  fd_set read_fds;
  int num_fds;
  int high_fd = conn_get_read_fds(&read_fds);
  if (!high_fd)
    {
      log_msg(LOG_ERR,
	      "%s:%d No connections initialized, shutting down.\n",
	      __FILE__, __LINE__);
      return 0;
    }
  /*
   * Initialize the signal sets.
   */
  sigemptyset(&block_set);
  sigemptyset(&empty_set);
  sigaddset(&block_set, SIGALRM);
  sigaddset(&block_set, SIGTERM);
  sigaddset(&block_set, SIGHUP);
  sigaddset(&block_set, SIGINT);
  for (;;)
    {
      fd_set read_test;
      fd_set write_test;
      fd_set *pwrite_test;
      /*
       * Block signals and check to see if any signals have arrived.
       */
      sigprocmask(SIG_BLOCK, &block_set, NULL);
      if (timeout)
	{
	  conn_tick();
	  timeout = 0;
	}
      if (terminate)
	{
	  log_msg(LOG_INFO, "%s:%d Received SIGTERM, shutting down.\n",
	      __FILE__, __LINE__);
	  return 0;
	}
      if (restart)
	{
	  log_msg(LOG_INFO, "%s:%d Received SIGHUP, restarting.\n",
		  __FILE__, __LINE__);
	  restart = 0;
	  return 1;
	}
      if (terminate_int)
	{
	  log_msg(LOG_INFO, "%s:%d Received SIGINT, shutting down.\n",
		  __FILE__, __LINE__);
	  return 0;
	}
      /*
       * Check and service file descriptors.
       */
      read_test = read_fds;
      pwrite_test = conn_get_write_fds(&write_test) ? &write_test : NULL;
      num_fds = pselect(high_fd, &read_test, pwrite_test, NULL, NULL,
			&empty_set);
      if (num_fds < 0)
	{
	  if (errno == EINTR) continue;
	  else
	    {
	      log_msg(LOG_ERR, "%s:%d pselect() failed : %s\n", __FILE__,
		      __LINE__, strerror(errno));
	      return 0;
	    }
	}
      if (conn_service_fds(&read_test, pwrite_test, &num_fds))
	{
	  high_fd = conn_get_read_fds(&read_fds);
	  if (!high_fd)
	    {
	      log_msg(LOG_INFO,
		      "%s:%d No remaining connections, shutting down.\n",
		      __FILE__, __LINE__);
	      return 0;
	    }
	}
    }
  return 0; /* Should never get here. */
}

/*
 * Description : Sets up signal handlers.
 *
 * Arguments : None.
 *
 * Return Value : Zero if successful.
 *                Non-zero if sigaction() fails.
 */
static int set_signals(void)
{
  struct sigaction sa;
  log_msg(LOG_DEBUG, "%s:%d Initializing signal handlers.\n", __FILE__,
	  __LINE__);
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = sig_alrm;
  if (sigaction(SIGALRM, &sa, NULL))
    {
      log_msg(LOG_ERR,
	      "%s:%d Error setting signal action for SIGALRM : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  sa.sa_handler = sig_term;
  if (sigaction(SIGTERM, &sa, NULL))
    {
      log_msg(LOG_ERR,
	      "%s:%d Error setting signal action for SIGTERM : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  sa.sa_handler = sig_hup;
  if (sigaction(SIGHUP, &sa, NULL))
    {
      log_msg(LOG_ERR,
	      "%s:%d Error setting signal action for SIGHUP : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  sa.sa_handler = sig_int;
  if (sigaction(SIGINT, &sa, NULL))
    {
      log_msg(LOG_ERR,
	      "%s:%d Error setting signal action for SIGINT : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  sa.sa_handler = sig_segv;
  if (sigaction(SIGSEGV, &sa, NULL))
    {
      log_msg(LOG_ERR,
	      "%s:%d Error setting signal action for SIGSEGV : %s\n",
	      __FILE__, __LINE__, strerror(errno));
      return -1;
    }
  return 0;
}

/*
 * Description : Signal handler for SIGALRM.
 *
 * Arguments : signo - Signal number.
 *
 * Return Value : None.
 */
static void sig_alrm(int signo)
{
  timeout = 1;
  return;
}

/*
 * Description : Signal handler for SIGTERM.
 *
 * Arguments : signo - Signal number.
 *
 * Return Value : None.
 */
static void sig_term(int signo)
{
  terminate = 1;
  return;
}

/*
 * Description : Signal handler for SIGHUP.
 *
 * Arguments : signo - Signal number.
 *
 * Return Value : None.
 */
static void sig_hup(int signo)
{
  restart = 1;
  return;
}

/*
 * Description : Signal handler for SIGINT.
 *
 * Arguments : signo - Signal number.
 *
 * Return Value : None.
 */
static void sig_int(int signo)
{
  terminate_int = 1;
  return;
}

/*
 * Description : Signal handler for SIGSEGV.
 *
 * Arguments : signo - Signal number.
 *
 * Return Value : Does not return.
 */
static void sig_segv(int signo)
{
  log_msg(LOG_ERR, "%s:%d Received SIGSEGV, unclean shutdown.\n",
	  __FILE__, __LINE__);
  exit(0);
  return;
}

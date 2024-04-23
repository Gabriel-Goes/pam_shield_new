/*
  pam_shield_lib.h

  Copyright (C) 2007-2024
  Walter de Jong <walter@heiho.net>
  Jonathan Niehof <jtniehof@gmail.com>
  Jeffrey Clark <h0tw1r3@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <gdbm.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "pam_shield.h"

#pragma GCC visibility push(hidden)

#define DEFAULT_MAX_CONNS 10
#define DEFAULT_INTERVAL 60L
#define DEFAULT_RETENTION (3600L * 24L)

#define MAX_LINE 1024

#define OPT_DEBUG 0x001
#define OPT_BLOCK_ALL 0x002 /* block all, including known users */
#define OPT_DRYRUN 0x004
#define OPT_LISTDB 0x008
#define OPT_MISSING_DNS 0x010     /* allow missing DNS */
#define OPT_MISSING_REVERSE 0x020 /* allow missing reverse DNS */
#define OPT_FORCE 0x040           /* purge unexpired entries */
#define OPT_REMOVEIP 0x080
#define OPT_SYNC 0x100

extern int options;
extern GDBM_FILE dbf;

extern char *conffile;
extern char *dbfile;
extern char *trigger_cmd;
extern char *removeip;

/* white lists of addresses */
extern ip_list *allow_ipv4_list;
extern ip_list *allow_ipv6_list;
extern name_list *allow_names;

extern int max_conns;
extern long interval;
extern long retention;

extern time_t this_time;

void logmsg(int level, const char *fmt, ...);

ip_list *new_ip_list(void);

void destroy_ip_list(ip_list *list);

void add_ip_list(ip_list **root, ip_list *ip);

/*
  try to match an IP number against the allow list
  returns 1 if it matches
*/
int match_ipv4_list(unsigned char *saddr);

int match_ipv6_list(unsigned char *saddr);

/*
  name_lists are hostnames and/or network names
*/
name_list *new_name_list(char *name);

void destroy_name_list(name_list *list);

void add_name_list(name_list **root, name_list *n);

/*
  see if 'name' matches our whitelist
  return 1 if it does
*/
int match_name_list(char *name);

/* initialize variables */
int init_module(void);

void deinit_module(void);

/* strip leading and trailing whitespace from a string */
void strip(char *str);

/*
  multipliers:
    1s	second
    1m	minute
    1h	hour
    1d	day
    1w	week
    1M	month
    1y	year

  default is 1
  returns 0 on error
*/
long get_multiplier(char *str);

/*
  generate bitmask from '/24' notation

  mask is struct in_addr.saddr, size is the size of the array
  (4 for IPv4, 16 for IPv6)
*/
void ip_bitmask(int bits, unsigned char *mask, int size);

/*
  allow network/netmask, for both IPv4 and IPv6
  netmask can be in canonical or decimal notation
*/
int allow_ip(char *ipnum, int line_no);

/* read configuration file */
int read_config(void);

/*
  print the IP number of a db_record
  return NULL on error, or buf on success
*/
const char *print_ip(_pam_shield_db_rec_t *record, char *buf, int buflen);

/* run external command */
int run_trigger(char *cmd, _pam_shield_db_rec_t *record);

int expire_record(_pam_shield_db_rec_t *record);

/* gdbm has encountered a fatal error */
void fatal_func(const char *str);

#pragma GCC visibility pop

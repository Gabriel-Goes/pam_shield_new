/*
    shield_purge.c

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

#include "pam_shield.h"

#include <getopt.h>
#include <libgen.h>

#include "pam_shield_lib.h"

void logmsg(int level, const char *fmt, ...) {
  va_list varargs;

  if (level == LOG_DEBUG && !(options & OPT_DEBUG))
    return;

  va_start(varargs, fmt);
  vfprintf(stderr, fmt, varargs);
  fprintf(stderr, "\n");
  va_end(varargs);
}

static void usage(char *progname) {
  printf("shield-purge " PAM_SHIELD_VERSION "\n"
         "usage: %s <options>\n"
         "options:\n"
         "  -h, --help        Display this information\n"
         "  -c, --conf=file   Specify config file (default: " DEFAULT_CONFFILE ")\n"
         "  -d, --debug       Verbose output for debugging purposes\n"
         "  -n, --dry-run     Do not perform any updates\n"
         "  -l, --list        List all database entries\n"
         "  -f, --force       Delete all entries, even if unexpired\n"
         "  -r, --remove=ip   Remove IP from database\n"
         "  -s, --sync        Trigger sync for active records\n"
         "                    (rebuild/verify firewall rules)\n",
         basename(progname));

  printf("\n"
         "This program is part of the PAM-shield package.\n"
         "PAM-shield comes with ABSOLUTELY NO WARRANTY.  This is free software, "
         "and you\n"
         "are welcome to redistribute it under certain conditions.  See the GNU\n"
         "General Public Licence for details.\n"
         "\n"
         "Copyright (C) 2007-2011 by Walter de Jong <walter@heiho.net>\n"
         "Copyright 2010 Jonathan Niehof <jtniehof@gmail.com>\n");
  exit(1);
}

static void get_options(int argc, char **argv) {
  int opt;
  struct option long_options[] = {
      {"help", 0, NULL, 'h'},    {"debug", 0, NULL, 'd'}, {"conf", 1, NULL, 'c'},
      {"dry-run", 0, NULL, 'n'}, {"list", 0, NULL, 'l'},  {"force", 0, NULL, 'f'},
      {"remove", 1, NULL, 'r'},  {"sync", 0, NULL, 's'},  {NULL, 0, NULL, 0},
  };

  while ((opt = getopt_long(argc, argv, "hdc:nlfr:s", long_options, NULL)) != -1) {
    switch (opt) {
    case 'h':
    case '?':
      usage(argv[0]);

    case 'd':
      options |= OPT_DEBUG;
      logmsg(LOG_DEBUG, "logging debug info");
      break;

    case 'c':
      if (optarg == NULL || !*optarg) {
        logmsg(LOG_ERR, "missing filename");
        exit(1);
      }
      if ((conffile = strdup(optarg)) == NULL) {
        logmsg(LOG_ERR, "out of memory");
        exit(-1);
      }
      break;

    case 'n':
      options |= OPT_DRYRUN;
      logmsg(LOG_DEBUG, "performing dry-run");
      break;

    case 'l':
      options |= OPT_LISTDB;
      logmsg(LOG_DEBUG, "list database");
      break;

    case 'f':
      options |= OPT_FORCE;
      logmsg(LOG_DEBUG, "force purge");
      break;

    case 'r':
      options |= OPT_REMOVEIP;
      if (optarg == NULL || !*optarg) {
        logmsg(LOG_ERR, "missing ip");
        exit(1);
      }
      if ((removeip = strdup(optarg)) == NULL) {
        logmsg(LOG_ERR, "out of memory");
        exit(-1);
      }
      break;

    case 's':
      options |= OPT_SYNC;
      logmsg(LOG_DEBUG, "sync");
      break;

    default:
      logmsg(LOG_ERR, "bad command line option");
      exit(1);
    }
  }
}

/* lists one record from the DB */
static void print_record(_pam_shield_db_rec_t *record) {
  char ipbuf[INET6_ADDRSTRLEN];
  unsigned int i;
  char *time = ctime(&record->trigger_active);

  time[strlen(time) - 1] = '\0';

  print_ip(record, ipbuf, INET6_ADDRSTRLEN);

  printf("\n    {\n"
         "      \"ip\": \"%s\",\n"
         "      \"max_entries\": %u,\n"
         "      \"count\": %u,\n"
         "      \"trigger_active\": \"%s\",\n"
         "      \"timestamps\": [\n",
         ipbuf, record->max_entries, record->count, (record->trigger_active > 0) ? time : "");
  for (i = 0; i < record->max_entries; i++)
    if (record->timestamps[i] > 0) {
      time = ctime(&record->timestamps[i]);
      time[strlen(time) - 1] = '\0';
      printf("        \"%s\"%s\n", time, (record->timestamps[(i + 1)] > 0) ? "," : "");
    }

  printf("      ]\n    }");
}

/*
    list database entries
    this is also mostly meant for debugging and looking at what's in the DB
*/
static void list_db(void) {
  _pam_shield_db_rec_t *record;
  datum key, next_key, data;

  key = gdbm_firstkey(dbf);

  if (key.dptr == NULL) {
    printf("database is empty\n");
    return;
  }

  printf("{\n  \"db\": [");

  while (key.dptr != NULL) {
    data = gdbm_fetch(dbf, key);

    if (data.dptr != NULL) {
      record = (_pam_shield_db_rec_t *)data.dptr;
      print_record(record);
    }
    next_key = gdbm_nextkey(dbf, key);
    free(key.dptr);
    key = next_key;
    if (data.dptr != NULL && key.dptr != NULL) {
      printf(",");
    }
  }

  printf("\n  ]\n}\n");
}

/* expire old entries from the database */
static void purge_db(void) {
  _pam_shield_db_rec_t *record;
  datum key, next_key, data;
  int deleted = 0; /*If any key deleted, order changes; must revisit all keys*/

  key = gdbm_firstkey(dbf);

  while (key.dptr != NULL) {
    data = gdbm_fetch(dbf, key);
    next_key = gdbm_nextkey(dbf, key);

    if (options & OPT_FORCE) {
      logmsg(LOG_DEBUG, "force-expiring entry");
      if (!(options & OPT_DRYRUN)) {
        gdbm_delete(dbf, key);
        deleted = 1;
      }
    } else if (data.dptr == NULL) {
      logmsg(LOG_DEBUG, "cleaning up empty key");
      if (!(options & OPT_DRYRUN)) {
        gdbm_delete(dbf, key);
        deleted = 1;
      }
    } else {
      record = (_pam_shield_db_rec_t *)data.dptr;

      /* store any changes */
      if (expire_record(record)) {
        if (!record->count && !record->trigger_active) {
          logmsg(LOG_DEBUG, "expiring entry");
          if (!(options & OPT_DRYRUN)) {
            gdbm_delete(dbf, key);
            deleted = 1;
          }
        } else {
          logmsg(LOG_DEBUG, "storing updated entry");
          if (!(options & OPT_DRYRUN))
            gdbm_store(dbf, key, data, GDBM_REPLACE);
        }
      }
      free(data.dptr);
    }
    free(key.dptr);
    key = next_key;
    if (deleted && !key.dptr) {
      deleted = 0;
      key = gdbm_firstkey(dbf);
    }
  }
}

/* remove ip from the database */
static int remove_ip(void) {
  _pam_shield_db_rec_t *record;
  datum key, next_key, data;
  int deleted = 0; /*If any key deleted, order changes; must revisit all keys*/
  char ipbuf[INET6_ADDRSTRLEN];

  key = gdbm_firstkey(dbf);

  while (key.dptr != NULL) {
    data = gdbm_fetch(dbf, key);
    next_key = gdbm_nextkey(dbf, key);

    if (data.dptr == NULL) {
      logmsg(LOG_DEBUG, "cleaning up empty key");
      if (!(options & OPT_DRYRUN)) {
        gdbm_delete(dbf, key);
        deleted = 1;
      }
    } else {
      record = (_pam_shield_db_rec_t *)data.dptr;

      print_ip(record, ipbuf, INET6_ADDRSTRLEN);
      if (!strcmp(removeip, ipbuf)) {
        logmsg(LOG_DEBUG, "remove entry: %s", ipbuf);
        deleted = 1;
        if (!(options & OPT_DRYRUN)) {
          record->trigger_active = (time_t)0L;
          run_trigger("del", record);
          gdbm_delete(dbf, key);
        }
      }
      free(data.dptr);
    }
    free(key.dptr);
    key = next_key;
    if (deleted && !key.dptr) {
      if (!(options & OPT_DRYRUN)) {
        key = gdbm_firstkey(dbf);
      }
      return 0;
    }
  }

  logmsg(LOG_ERR, "not found: %s", removeip);
  return 1;
}

int main(int argc, char **argv) {
  int retval = 0;
  init_module();

  read_config();
  get_options(argc, argv);

  this_time = time(NULL);

  if ((dbf = gdbm_open(dbfile, 512, GDBM_WRITER, (mode_t)0600, fatal_func)) == NULL) {
    logmsg(LOG_ERR, "failed to open db '%s' : %s", dbfile, gdbm_strerror(gdbm_errno));
    return -1;
  }
  if (options & OPT_LISTDB)
    list_db();
  else if (options & OPT_REMOVEIP)
    retval = remove_ip();
  else
    purge_db();

  gdbm_close(dbf);

  deinit_module();
  return retval;
}

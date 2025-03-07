/* Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifndef MYSYS_PRIV_INCLUDED
#define MYSYS_PRIV_INCLUDED

#include <my_global.h>
#include <my_sys.h>
#include <my_crypt.h>

C_MODE_START

#ifdef USE_SYSTEM_WRAPPERS
#include "system_wrappers.h"
#endif

#ifdef HAVE_GETRUSAGE
#include <sys/resource.h>
#endif

#include <my_pthread.h>

#ifdef HAVE_PSI_INTERFACE

#if !defined(HAVE_PREAD) && !defined(_WIN32)
extern PSI_mutex_key key_my_file_info_mutex;
#endif /* !defined(HAVE_PREAD) && !defined(_WIN32) */

#if !defined(HAVE_LOCALTIME_R) || !defined(HAVE_GMTIME_R)
extern PSI_mutex_key key_LOCK_localtime_r;
#endif /* !defined(HAVE_LOCALTIME_R) || !defined(HAVE_GMTIME_R) */

extern PSI_mutex_key key_BITMAP_mutex, key_IO_CACHE_append_buffer_lock,
  key_IO_CACHE_SHARE_mutex, key_KEY_CACHE_cache_lock, key_LOCK_alarm,
  key_my_thread_var_mutex, key_THR_LOCK_charset, key_THR_LOCK_heap,
  key_THR_LOCK_lock, key_THR_LOCK_malloc,
  key_THR_LOCK_mutex, key_THR_LOCK_myisam, key_THR_LOCK_net,
  key_THR_LOCK_open, key_THR_LOCK_threads, key_LOCK_uuid_generator,
  key_TMPDIR_mutex, key_THR_LOCK_myisam_mmap, key_LOCK_timer;

extern PSI_cond_key key_COND_alarm, key_COND_timer, key_IO_CACHE_SHARE_cond,
  key_IO_CACHE_SHARE_cond_writer, key_my_thread_var_suspend,
  key_THR_COND_threads;

#ifdef USE_ALARM_THREAD
extern PSI_thread_key key_thread_alarm;
#endif /* USE_ALARM_THREAD */
extern PSI_thread_key key_thread_timer;
extern PSI_rwlock_key key_SAFEHASH_mutex;

#endif /* HAVE_PSI_INTERFACE */

extern PSI_stage_info stage_waiting_for_table_level_lock;

extern mysql_mutex_t THR_LOCK_malloc, THR_LOCK_open, THR_LOCK_keycache;
extern mysql_mutex_t THR_LOCK_lock, THR_LOCK_net;
extern mysql_mutex_t THR_LOCK_charset;

#include <mysql/psi/mysql_file.h>

#ifdef HAVE_PSI_INTERFACE
#ifdef HUGETLB_USE_PROC_MEMINFO
extern PSI_file_key key_file_proc_meminfo;
#endif /* HUGETLB_USE_PROC_MEMINFO */
extern PSI_file_key key_file_charset, key_file_cnf;
#endif /* HAVE_PSI_INTERFACE */

typedef struct {
  ulonglong counter;
  uint block_length, last_block_length;
  uchar key[MY_AES_BLOCK_SIZE];
  ulonglong inbuf_counter;
} IO_CACHE_CRYPT;

extern int (*_my_b_encr_read)(IO_CACHE *info,uchar *Buffer,size_t Count);
extern int (*_my_b_encr_write)(IO_CACHE *info,const uchar *Buffer,size_t Count);

#ifdef SAFEMALLOC
void *sf_malloc(size_t size, myf my_flags);
void *sf_realloc(void *ptr, size_t size, myf my_flags);
void sf_free(void *ptr);
size_t sf_malloc_usable_size(void *ptr, my_bool *is_thread_specific);
#else
#define sf_malloc(X,Y)    malloc(X)
#define sf_realloc(X,Y,Z) realloc(X,Y)
#define sf_free(X)      free(X)
#endif

/*
  EDQUOT is used only in 3 C files only in mysys/. If it does not exist on
  system, we set it to some value which can never happen.
*/
#ifndef EDQUOT
#define EDQUOT (-1)
#endif

void my_error_unregister_all(void);

#ifndef O_PATH        /* not Linux */
#if defined(O_SEARCH) /* Illumos */
#define O_PATH O_SEARCH
#elif defined(O_EXEC) /* FreeBSD */
#define O_PATH O_EXEC
#endif
#endif

#ifdef O_PATH
#define HAVE_OPEN_PARENT_DIR_NOSYMLINKS
const char *my_open_parent_dir_nosymlinks(const char *pathname, int *pdfd);
#define NOSYMLINK_FUNCTION_BODY(AT,NOAT)                                \
  int dfd, res;                                                         \
  const char *filename= my_open_parent_dir_nosymlinks(pathname, &dfd);  \
  if (filename == NULL) return -1;                                      \
  res= AT;                                                              \
  if (dfd >= 0) close(dfd);                                             \
  return res;
#elif defined(HAVE_REALPATH) && defined(PATH_MAX)
#define NOSYMLINK_FUNCTION_BODY(AT,NOAT)                                \
  char buf[PATH_MAX+1];                                                 \
  if (realpath(pathname, buf) == NULL) return -1;                       \
  if (strcmp(pathname, buf)) { errno= ENOTDIR; return -1; }             \
  return NOAT;
#elif defined(HAVE_REALPATH)
#define NOSYMLINK_FUNCTION_BODY(AT,NOAT)                                \
  char *buf= realpath(pathname, NULL);                                  \
  int res;                                                              \
  if (buf == NULL) return -1;                                           \
  if (strcmp(pathname, buf)) { errno= ENOTDIR; res= -1; }               \
  else res= NOAT;                                                       \
  free(buf);                                                            \
  return res;
#else
#define NOSYMLINK_FUNCTION_BODY(AT,NOAT)                                \
  return NOAT;
#endif

#ifndef _WIN32
#define CREATE_NOSYMLINK_FUNCTION(PROTO,AT,NOAT)                        \
static int PROTO { NOSYMLINK_FUNCTION_BODY(AT,NOAT) }
#else
#define CREATE_NOSYMLINK_FUNCTION(PROTO,AT,NOAT)
#endif

#ifdef _WIN32
#include <sys/stat.h>
/* my_winfile.c exports, should not be used outside mysys */
extern File     my_win_open(const char *path, int oflag);
extern int      my_win_close(File fd);
extern size_t   my_win_read(File fd, uchar *buffer, size_t  count);
extern size_t   my_win_write(File fd, const uchar *buffer, size_t count);
extern size_t   my_win_pread(File fd, uchar *buffer, size_t count, 
                             my_off_t offset);
extern size_t   my_win_pwrite(File fd, const uchar *buffer, size_t count, 
                              my_off_t offset);
extern my_off_t my_win_lseek(File fd, my_off_t pos, int whence);
extern int      my_win_chsize(File fd,  my_off_t newlength);
extern FILE*    my_win_fopen(const char *filename, const char *type);
extern File     my_win_fclose(FILE *file);
extern File     my_win_fileno(FILE *file);
extern FILE*    my_win_fdopen(File Filedes, const char *type);
extern int      my_win_stat(const char *path, struct _stati64 *buf);
extern int      my_win_fstat(File fd, struct _stati64 *buf);
extern int      my_win_fsync(File fd);
extern File     my_win_dup(File fd);
extern File     my_win_sopen(const char *path, int oflag, int shflag, int perm);
extern File     my_open_osfhandle(HANDLE handle, int oflag);


/*
  The following constants are related to retries when file operation fails with
  ERROR_FILE_SHARING_VIOLATION
*/
#define FILE_SHARING_VIOLATION_RETRIES  50
#define FILE_SHARING_VIOLATION_DELAY_MS 10


/* DBUG injecting of ERROR_FILE_SHARING_VIOLATION */
#ifndef DBUG_OFF
/* Open file, without sharing. if specific DBUG keyword is set */
#define DBUG_INJECT_FILE_SHARING_VIOLATION(filename)                          \
  FILE *fp= NULL;                                                             \
  do                                                                          \
  {                                                                           \
    DBUG_EXECUTE_IF("file_sharing_violation",                                 \
                    fp= _fsopen(filename, "r", _SH_DENYRW););                 \
  } while (0)

/* Close the file that causes ERROR_FILE_SHARING_VIOLATION.*/
#define DBUG_CLEAR_FILE_SHARING_VIOLATION()                                   \
  do                                                                          \
  {                                                                           \
    if (fp)                                                                   \
    {                                                                         \
      DWORD tmp_err= GetLastError();                                          \
      fclose(fp);                                                             \
      SetLastError(tmp_err);                                                  \
      fp= NULL;                                                               \
    }                                                                         \
  } while (0)

#else
#define DBUG_INJECT_FILE_SHARING_VIOLATION(filename) do {} while (0)
#define DBUG_CLEAR_FILE_SHARING_VIOLATION()  do {} while (0)
#endif

#endif

C_MODE_END

#endif

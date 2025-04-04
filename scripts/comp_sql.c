/* Copyright (c) 2004, 2010, Oracle and/or its affiliates. 
   Copyright (c) 2012, 2022, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1335  USA */

/*
  Originally written by Magnus Svensson
*/

/*
  Converts a SQL file into a C file that can be compiled and linked
  into other programs
*/

#include <my_config.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

/*
  This is an internal tool used during the build process only,
  - do not make a library just for this,
    which would make the Makefiles and the server link
    more complex than necessary,
  - do not duplicate the code either.
 so just add the sql_bootstrap.cc code as is.
*/
#include "../sql/sql_bootstrap.cc"

FILE *in, *out;

static void die(const char *fmt, ...)
{
  va_list args;

  /* Print the error message */
  fprintf(stderr, "FATAL ERROR: ");
  if (fmt)
  {
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
  else
    fprintf(stderr, "unknown error");
  fprintf(stderr, "\n");
  fflush(stderr);

  /* Close any open files */
  if (in)
    fclose(in);
  if (out)
    fclose(out);

  exit(1);
}

char *fgets_fn(char *buffer, size_t size, fgets_input_t input, int *error)
{
  char *line= fgets(buffer, (int)size, (FILE*) input);
  if (error)
    *error= (line == NULL) ? ferror((FILE*)input) : 0;
  return line;
}

#define MAX_COLUMN 16000

static void print_query(FILE *out, const char *query)
{
  const char *ptr= query;
  int column= 0;

  fprintf(out, "\"");
  while (*ptr)
  {
    if (column >= MAX_COLUMN)
    {
      /* Wrap to the next line, tabulated. */
      fprintf(out, "\"\n  \"");
      column= 2;
    }
    switch(*ptr)
    {
    case '\n':
      /*
        Preserve the \n character in the query text,
        and wrap to the next line, tabulated.
      */
      fprintf(out, "\\n\"\n  \"");
      column= 2;
      break;
    case '\r':
      /* Skipped */
      break;
    case '\"':
      fprintf(out, "\\\"");
      column+=2;
      break;
    case '\\':
      fprintf(out, "\\\\");
      column+=2;
      break;
    default:
      putc(*ptr, out);
      column++;
      break;
    }
    ptr++;
  }
  fprintf(out, "\\n\",\n");
}

int main(int argc, char *argv[])
{
  char query[MAX_BOOTSTRAP_QUERY_SIZE];
  char* struct_name= argv[1];
  char* infile_name= argv[2];
  char* outfile_name= argv[3];
  int rc;
  int query_length;
  int error= 0;
  char *err_ptr;

  if (argc != 4)
    die("Usage: comp_sql <struct_name> <sql_filename> <c_filename>");

  /* Open input and output file */
  if (!(in= fopen(infile_name, "r")))
    die("Failed to open SQL file '%s'", infile_name);
  if (!(out= fopen(outfile_name, "w")))
    die("Failed to open output file '%s'", outfile_name);

  fprintf(out, "/*\n");
  fprintf(out, "  Do not edit this file, it is automatically generated from:\n");
  fprintf(out, "  <%s>\n", infile_name);
  fprintf(out, "*/\n");
  fprintf(out, "#include <stdlib.h>\n"); /* NULL */
  fprintf(out, "const char* %s[]={\n", struct_name);

  for ( ; ; )
  {
    rc= read_bootstrap_query(query, &query_length,
                             (fgets_input_t) in, fgets_fn, &error);

    if (rc == READ_BOOTSTRAP_EOF)
      break;

    if (rc != READ_BOOTSTRAP_SUCCESS)
    {
      /* Get the most recent query text for reference. */
      err_ptr= query + (query_length <= MAX_BOOTSTRAP_ERROR_LEN ?
                                 0 : (query_length - MAX_BOOTSTRAP_ERROR_LEN));
      switch (rc)
      {
      case READ_BOOTSTRAP_ERROR:
        die("Failed to read the bootstrap input file. Return code (%d).\n"
            "Last query: '%s'\n", error, err_ptr);
        break;

      case READ_BOOTSTRAP_QUERY_SIZE:
        die("Failed to read the boostrap input file. Query size exceeded %d bytes.\n"
            "Last query: '%s'.\n", MAX_BOOTSTRAP_LINE_SIZE, err_ptr);
        break;
    
      default:
        die("Failed to read the boostrap input file. Unknown error.\n");
        break;
      }
    }

    print_query(out, query);
  }

  fprintf(out, "NULL\n};\n");

  fclose(in);
  fclose(out);

  exit(0);

}


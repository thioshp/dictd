/* dictfmt.c -- 
 * Created: Sun Jul 20 20:17:11 1997 by faith@acm.org
 * Revised: Sat Sep 27 23:47:04 2003 by faith@acm.org
 * Copyright 1997, 1998, 2003 Rickard E. Faith (faith@acm.org)
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 1, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * $Id: dictfmt.c,v 1.37 2003/12/30 16:35:16 cheusov Exp $
 *
 * Sun Jul 5 18:48:33 1998: added patches for Gutenberg's '1995 CIA World
 * Factbook' from David Frey <david@eos.lugs.ch>.
 *
 * v. 1.6 Mon, 25 Dec 2000 18:38:02 -0500 added -V, -L and --help options
 * Robert D. Hilliard <hilliard@debian.org>
 */

#include "dictP.h"
#include "str.h"

#include <maa.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#if HAVE_WCTYPE_H
#include <wctype.h>
#endif

#include <locale.h>

#if HAVE_GETOPT_H
#include <getopt.h>
#endif

#define FMT_MAXPOS  200
#define FMT_INDENT  0

#define JARGON    1
#define FOLDOC    2
#define EASTON    3
#define PERIODIC  4
#define HITCHCOCK 5
#define CIA1995   6
#define VERA      7

#define BSIZE 10240

static int  Debug;
static FILE *str;

static int utf8_mode     = 0;
static int bit8_mode     = 0;

static int allchars_mode = 0;

static int quiet_mode    = 0;

static const char *hw_separator = "";

static int         without_hw     = 0;
static int         without_header = 0;
static int         without_url    = 0;
static int         without_time   = 0;
static int         without_info   = 0;

static FILE *fmt_str;
static int  fmt_indent;
static int  fmt_pos;
static int  fmt_pending;
static int  fmt_hwcount;
static int  fmt_maxpos = FMT_MAXPOS;
static int  fmt_ignore_headword = 0;

static int ignore_hw_url       = 0;
static int ignore_hw_shortname = 0;
static int ignore_hw_info      = 0;

static const char *locale         = "C";

static str_Pool alphabet_pool = NULL;

static void init (const char *fn)
{
   maa_init (fn);

   alphabet_pool = str_pool_create ();
}

static int print_alphabet (const void *symbol, void *arg)
{
   printf ("%s: %s\n", (char *) arg, (const char *) symbol);
   return 0;
}

static void destroy (void)
{
   str_pool_destroy (alphabet_pool);
   alphabet_pool = NULL;

//   maa_shutdown ();
}

static void destroy_and_exit (int exit_status)
{
   destroy ();
   exit (exit_status);
}

static void fmt_openindex( const char *filename )
{
   char buffer[1024];

   if (!filename)
      return;

   if (utf8_mode || allchars_mode)
      snprintf( buffer, sizeof (buffer), "sort > %s\n", filename );
   else
      snprintf( buffer, sizeof (buffer), "sort -df > %s\n", filename );

   if (!(fmt_str = popen( buffer, "w" ))) {
      fprintf( stderr, "Cannot open %s for write\n", buffer );

      destroy_and_exit (1);
   }
}

static void fmt_newline( void )
{
   int i;

   if (fmt_ignore_headword){
      return;
   }

   fputc('\n', str);
   for (i = 0; i < fmt_indent; i++) fputc(' ', str);
   fmt_pos = fmt_indent;
   fmt_pending = 0;
}

static void fmt_string( const char *s )
{
   char *sdup = NULL;
   char *pt   = NULL;
   char *p    = NULL;
#if 0
   char *t;
#endif
   size_t  len;

   assert (s);

   if (fmt_ignore_headword){
      return;
   }

   sdup = malloc( strlen(s) + 1 );
   p = pt = sdup;

#if 1
   strcpy( sdup, s );
#else
   for (t = sdup; *s; s++) {
      if (*s == '_') *t++ = ' ';
      else *t++ = *s;
   }
   *t = '\0';
#endif

   while ((pt = strchr(pt, ' '))) {
      *pt++ = '\0';

      if (utf8_mode){
	 len = mbstowcs (NULL, p, 0);
	 if (len == (size_t) -1)
	    err_fatal (__FUNCTION__, "invalid utf-8 string '%s'\n", s);
      }else{
	 len = strlen (p);
      }

      if (fmt_pending && fmt_pos + len > fmt_maxpos) {
	 fmt_newline();
      }
      if (fmt_pending) {
	 fputc(' ', str);
	 ++fmt_pos;
	 fmt_pending = 0;
      }
      fprintf( str, "%s", p );
      fmt_pos += len;
      p = pt;
      fmt_pending = 1;
   }
   
   len = strlen(p);
   if (fmt_pending && fmt_pos + len > fmt_maxpos) {
      fmt_newline();
   }
   if (len && fmt_pending) {
      fputc(' ', str);
      ++fmt_pos;
      fmt_pending = 0;
   }
   if (!len) {
      fmt_pending = 1;
   } else {
      fprintf( str, "%s", p );
      fmt_pos += len;
   }

   free(sdup);
}

#ifdef HAVE_UTF8
/*
  makes anagram of the 8-bit string 's'
  if length == -1 then str is 0-terminated string
*/
static void stranagram_8bit (char *s, int length)
{
   char* i = s;
   char* j;
   char v;

   assert (s);

   if (length == -1)
      length = strlen (s);

   j = s + length - 1;

   while (i < j){
      v = *i;
      *i = *j;
      *j = v;

      ++i;
      --j;
   }
}

/*
  makes anagram of the utf-8 string 's'
  Returns non-zero if success, 0 otherwise
*/
static int stranagram_utf8 (char *s)
{
   size_t len;
   char   *p;

   mbstate_t ps;

   assert (s);

   memset (&ps,  0, sizeof (ps));

   for (p = s; *p; ){
      len = mbrlen (p, MB_CUR_MAX, &ps);
      if ((int) len < 0)
	 return 0; /* not a UTF-8 string */

      if (len > 1)
	 stranagram_8bit (p, len);

      p += len;
   }

   stranagram_8bit (s, -1);
   return 1;
}

#endif

/*
  Remove spaces at the end of the string
 */
static char *trim_right (char *s)
{
   wchar_t mbc;
   int len;

   if (!utf8_mode){
      len = strlen (s);

      while (len > 0 && isspace ((unsigned char) s [len - 1])){
	 s [--len] = 0;
      }

      return s;
   }else{
#ifdef HAVE_UTF8
      if (!stranagram_utf8 (s))
	 abort ();

      do {
	 len = mbtowc (&mbc, s, MB_CUR_MAX);
	 assert (len >= 0);

	 if (len == 0 || !iswspace (mbc))
	    break;

	 s += len;
      }while (1);

      if (!stranagram_utf8 (s))
	 abort ();

#else
      abort();
#endif

      return s;
   }
}

/*
  Remove spaces at the beginning of the string
 */
static char *trim_left (char *s)
{
   wchar_t mbc;
   int len;

   if (!utf8_mode){
      while (isspace((unsigned char) *s)){
	 ++s;
      }

      return s;
   }else{
#ifdef HAVE_UTF8
      do {
	 len = mbtowc (&mbc, s, MB_CUR_MAX);
	 assert (len >= 0);

	 if (len == 0 || !iswspace (mbc))
	    break;

	 s += len;
      }while (1);

#else
      abort();
#endif

      return s;
   }
}

static void write_hw_to_index (const char *word, int start, int end)
{
   int len = 0;
   char *new_word = NULL;

   if (!word)
       return;

   len = strlen (word);

   if (len > 0){
      new_word = malloc (len + 1);
      if (!new_word){
	 perror ("malloc failed");

	 destroy_and_exit (1);
      }

      if (tolower_alnumspace (word, new_word, allchars_mode, utf8_mode)){
	 fprintf (stderr, "'%s' is not a UTF-8 string", word);

	 destroy_and_exit (1);
      }

      word = trim_right (new_word);

      fprintf( fmt_str, "%s\t%s\t", word, b64_encode(start) );
      fprintf( fmt_str, "%s\n", b64_encode(end-start) );

      free (new_word);
   }
}

static int contain_nonascii_symbol (const char *word)
{
   if (!word)
      return 0;

   while (*word){
      if (!isascii ((unsigned char) *word))
	 return 1;

      ++word;
   }

   return 0;
}

static void update_alphabet (const char *word)
{
   char *p;
   size_t len = 0;
   mbstate_t ps;
   char old_char;

   if (!word)
      return;

   len = strlen (word);
   p = (char *) alloca (len + 1);
   tolower_alnumspace (word, p, allchars_mode, utf8_mode);

   memset (&ps, 0, sizeof (ps));

   while (*p){
      len = mbrlen (p, MB_CUR_MAX, &ps);
      assert ((int) len >= 0);

      old_char = p [len];
      p [len] = 0;
      str_pool_find (alphabet_pool, p);
      p [len] = old_char;

      p += len;
   }
}

static void fmt_newheadword( const char *word )
{
   static char prev[1024] = "";
   static int  start = 0;
   static int  end;
   char *      sep   = NULL;
   char *      p;

   update_alphabet (word);

   if (
      word &&
      (!strcmp (word, "00-database-url") ||
       !strcmp (word, "00databaseurl")))
   {
      if (ignore_hw_url){
	 fmt_ignore_headword = 1;
	 return;
      }

      /* we will ignore all the following occurences of 00-database-url*/
      ignore_hw_url = 1;
   }

   if (
      word &&
      (!strcmp (word, "00-database-short") ||
       !strcmp (word, "00databaseshort")))
   {
      if (ignore_hw_shortname){
	 fmt_ignore_headword = 1;
	 return;
      }

      /* we will ignore all the following occurences of 00-database-short*/
      ignore_hw_shortname = 1;
   }

   if (
      word &&
      (!strcmp (word, "00-database-info") ||
       !strcmp (word, "00databaseinfo")))
   {
      if (ignore_hw_info){
	 fmt_ignore_headword = 1;
	 return;
      }

      /* we will ignore all the following occurences of 00-database-short*/
      ignore_hw_info = 1;
   }

   fmt_ignore_headword = 0;

   if (locale [0] == 'C' && locale [1] == 0){
      if (contain_nonascii_symbol (word)){
	 fprintf (stderr, "\n8-bit head word \"%s\"is encountered while \"C\" locale is used\n", word);
	 destroy_and_exit (1);
      }
   }

   fmt_indent = 0;
//   fmt_newline();
   fflush(stdout);
   end = ftell(str);

   if (word && !without_hw){
      fmt_string (word);
      fmt_newline();
   }

   if (fmt_str && *prev) {
      p = prev;
      do {
	 sep = NULL;
	 if (hw_separator [0] &&
	     strncmp (prev, "00-database", 11) &&
	     strncmp (prev, "00database", 10))
	 {
	    sep = strstr (p, hw_separator);
	    if (sep)
	       *sep = 0;
	 }

	 write_hw_to_index (p, start, end);

	 if (!sep)
	    break;

	 p = sep + strlen (hw_separator);
      }while (1);
   }

   if (word) {
      strlcpy(prev, word, sizeof (prev));

      start = end;
   }

   if (!quiet_mode){
      if (fmt_hwcount && !(fmt_hwcount % 100)) {
	 fprintf( stderr, "%10d headwords\r", fmt_hwcount );
      }
   }

   ++fmt_hwcount;
}

static void fmt_closeindex( void )
{
   fmt_newheadword (NULL);
   if (fmt_str){
      pclose( fmt_str );
   }

   if (!quiet_mode){
      fprintf( stderr, "%12d headwords\n", fmt_hwcount );
   }
}

static void banner( FILE *out_stream )
{
   fprintf( out_stream, "dictfmt v. %s December 2000 \n", DICT_VERSION );
   fprintf( out_stream,
         "Copyright 1997-2000 Rickard E. Faith (faith@cs.unc.edu)\n\n" );
}

static void license( void )
{
   static const char *license_msg[] = {
     "This program is free software; you can redistribute it and/or modify it",
     "under the terms of the GNU General Public License as published by the",
     "Free Software Foundation; either version 1, or (at your option) any",
     "later version.",
     "",
     "This program is distributed in the hope that it will be useful, but",
     "WITHOUT ANY WARRANTY; without even the implied warranty of",
     "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU",
     "General Public License for more details.",
     "",
     "You should have received a copy of the GNU General Public License along",
     "with this program; if not, write to the Free Software Foundation, Inc.,",
     "675 Mass Ave, Cambridge, MA 02139, USA.",
   0 };
   const char        **p = license_msg;
   
   banner ( stdout );
   while (*p) fprintf( stdout, "   %s\n", *p++ );
}

static void help( FILE *out_stream )
{
   static const char *help_msg[] = {
   "Usage: dictfmt [-c5|-t|-e|-f|-h|-j|-p] -u url -s name [options] basename",
   "Create a dictionary databse and index file for use by a dictd server",
   "",
     "-c5       headwords are preceded by a line containing at least \n\
                5 underscore (_) characters",
     "-t        implies -c5, --without-headword and --without-info options",
     "-e        file is in html format",
     "-f        headwords start in col 0, definitions start in col 8",
     "-j        headwords are set off by colons",
     "-p        headwords are preceded by %p, with %d on following line",
     "-u <url>  URL of site where database was obtained",
     "-s <name> name of the database", 
     "--license\n\
-L        display copyright and license information",
     "--version\n\
-V        display version information",
     "-D        debug",
"--quiet\n\
--silent\n\
-q        quiet operation",
"--help    display this help message", 
"--locale   <locale> specifies the locale used for sorting.\n\
           if no locale is specified, the \"C\" locale is used.",
"--allchars all characters (not only alphanumeric and space)\n\
           will be used in search if this argument is supplied",
"--headword-separator <sep> sets head word separator which allows\n\
           several words to have the same definition\n\
           Example: autumn%%%fall can be used\n\
           if '--headword-separator %%%' is supplied",
"--without-headword   head words will not be copied to .dict file",
"--without-header     header will not be copied to DB info entry",
"--without-url        URL will not be copied to DB info entry",
"--without-time       time of creation will not be copied to DB info entry",
"--without-info       DB info entry will not be created.\n\
                     This may be useful if 00-database-info headword\n\
                     is expected from stdin (dictunformat outputs it).",
      0 };
   const char        **p = help_msg;

   banner( out_stream );
   while (*p) fprintf( out_stream, "%s\n", *p++ );
}

static void set_utf8bit_mode (const char *loc)
{
   char *locale_copy;
   locale_copy = strdup (loc);
   strlwr_8bit (locale_copy);

   utf8_mode =
      NULL != strstr (locale_copy, "utf-8") ||
      NULL != strstr (locale_copy, "utf8");

#if !HAVE_UTF8
   if (utf8_mode){
      err_fatal (
	 __FUNCTION__,
	 "utf-8 support is disabled at compile time\n");
   }
#endif

   bit8_mode = !utf8_mode && (locale_copy [0] != 'c' || locale_copy [1] != 0);

   free (locale_copy);
}

static const char string_unknown [] = "unknown";
static const char *url = string_unknown;
static const char *sname = string_unknown;

static void fmt_headword_for_url (void)
{
   fmt_newheadword("00-database-url");
   fmt_string( "     " );
   fmt_string( url );
   fmt_newline ();

   ignore_hw_url = 1;
}

static void fmt_headword_for_alphabet (void)
{
   const char *key;
   size_t len;
   size_t sum_size = 0;
   str_Position pos;
   char *alphabet;

   fmt_newheadword("00-database-alphabet");

   STR_ITERATE (alphabet_pool, pos, key){
      sum_size += strlen (key);
   }

   alphabet = xmalloc (sum_size + 1);
   alphabet [0] = 0;

   STR_ITERATE (alphabet_pool, pos, key){
      strcat (alphabet, key);
   }

   fmt_string (alphabet);

   xfree (alphabet);

   fmt_newline ();
}

static void fmt_headword_for_shortname (void)
{
   fmt_newheadword("00-database-short");
   fmt_string ("00-database-short");
   fmt_newline ();
   fmt_string( "     " );
   fmt_string( sname );
   fmt_newline ();

   ignore_hw_shortname = 1;
}

static void fmt_headword_for_info (void)
{
   time_t     t;
   char       buffer[BSIZE];

   fmt_newheadword("00-database-info");

   if (!without_time){
      fmt_string("This file was converted from the original database on:" );
      fmt_newline();
      time(&t);
      snprintf( buffer, sizeof (buffer), "          %25.25s", ctime(&t) );
      fmt_string( buffer );
      fmt_newline();
      fmt_newline();
   }

   if (!without_url){
      fmt_string( "The original data is available from:" );
      fmt_newline();
      fmt_string( "     " );
      fmt_string( url );
      fmt_newline();
      fmt_newline();
   }

   if (!without_header){
      fmt_string(
	 "The original data was distributed with the notice shown below."
	 " No additional restrictions are claimed.  Please redistribute"
	 " this changed version under the same conditions and restriction"
	 " that apply to the original version." );
      fmt_newline();
      fmt_newline();
   }
}

static void fmt_headword_for_utf8 (void)
{
   if (utf8_mode){
      fmt_newheadword("00-database-utf8");
      fmt_newline();
   }
}

static void fmt_headword_for_8bit (void)
{
   if (bit8_mode){
      fmt_newheadword("00-database-8bit");
      fmt_newline();
   }
}

static void fmt_headword_for_allchars (void)
{
   if (allchars_mode){
      fmt_newheadword("00-database-allchars");
      fmt_newline();
   }
}

/* ...before reading the input */
static void fmt_predefined_headwords_before ()
{
   fmt_headword_for_utf8 ();
   fmt_headword_for_8bit ();
   fmt_headword_for_allchars ();

   if (url != string_unknown){
      /*
	-u option is not applied and we add 00-database-url headword
      */
      fmt_headword_for_url ();
   }

   if (sname != string_unknown){
      /*
	-s option is not applied and we add 00-database-short headword
      */
      fmt_headword_for_shortname ();
   }

   if (!without_info){
      fmt_headword_for_info ();
   }
}

/* ...after reading the input */
static void fmt_predefined_headwords_after ()
{
   fmt_headword_for_url ();
   fmt_headword_for_shortname ();
   fmt_headword_for_alphabet ();
}

int main( int argc, char **argv )
{
   int        c;
   int        type = 0;
   char       buffer[BSIZE];
   char       buffer2[BSIZE];
   char       indexname[1024];
   char       dataname[1024];

   int        header = 0;
   char       *pt;
   char       *s, *d;
   unsigned char *buf;

   struct option      longopts[]  = {
      { "help",       0, 0, 501 },
      { "locale",     1, 0, 502 },
      { "allchars",   0, 0, 503 },
      { "headword-separator",   1, 0, 504 },
      { "without-headword",     0, 0, 505 },
      { "without-header",       0, 0, 506 },
      { "without-url",          0, 0, 507 },
      { "without-time",         0, 0, 508 },
      { "without-info",         0, 0, 509 },
      { "quiet",                0, 0, 'q' },
      { "silent",               0, 0, 'q' },
      { "version",              0, 0, 'V' },
      { "license",              0, 0, 'L' },
   };

   init (argv[0]);

   while ((c = getopt_long( argc, argv, "qVLjvfephDu:s:c:t",
                                    longopts, NULL )) != EOF)
      switch (c) {
      case 'q': quiet_mode = 1;            break;
      case 'L': license();
	 destroy_and_exit (1);
	 break;
      case 'V': banner( stdout );
	 destroy_and_exit (1);
	 break;
      case 501: help( stdout );
	 destroy_and_exit (1);
	 break;         
      case 'j': type = JARGON;             break;
      case 'f': type = FOLDOC; fmt_maxpos=79; break;
      case 'e': type = EASTON;             break;
      case 'p': type = PERIODIC;           break;
      case 'h':
	 type = HITCHCOCK;
	 without_hw = 1;
	 break;
      case 'v': type = VERA; fmt_maxpos=75;break;
      case 'D': ++Debug;                   break;
      case 'u': url = optarg;              break;
      case 's': sname = optarg;            break;
      case 'c':                            
	 switch (*optarg) {                
	 case '5': type = CIA1995;
	    break;
	 default:
	    fprintf( stderr,
		     "Only CIA 1995 (-c5) currently supported\n" );

	    destroy_and_exit (1);
	 }

	 break;
      case 502: locale         = optarg;      break;
      case 503: allchars_mode  = 1;           break;
      case 504: hw_separator   = optarg;      break;
      case 505: without_hw     = 1;           break;
      case 506: without_header = 1;           break;
      case 507: without_url    = 1;           break;
      case 508: without_time   = 1;           break;
      case 509: without_info   = 1;           break;
      case 't':
	 without_info = 1;
	 without_hw   = 1;
	 type = CIA1995;
	 break;
      default:
         help (stderr);
	 
	 destroy_and_exit (1);
      }

   if (optind + 1 != argc) {
      help (stderr);

      destroy_and_exit (1);
   }

   set_utf8bit_mode (locale);

   if (utf8_mode)
      setenv("LC_ALL", "C", 1); /* this is for 'sort' subprocess */
   else
      setenv("LC_ALL", locale, 1); /* this is for 'sort' subprocess */

   if (!setlocale(LC_ALL, locale)){
      fprintf (stderr, "invalid locale '%s'\n", locale);

      destroy_and_exit (2);
   }

   if (
      -1 == snprintf (
	 indexname, sizeof (indexname), "%s.index", argv[optind] )||
      -1 == snprintf (
	 dataname,  sizeof (dataname), "%s.dict", argv[optind] ))
   {
      err_fatal (__FUNCTION__, "Too long filename\n");
   }

   fmt_openindex( indexname );
   if (Debug) {
      str = stdout;
   } else {
      if (!(str = fopen(dataname, "w"))) {
	 fprintf(stderr, "Cannot open %s for write\n", dataname);

	 destroy_and_exit (1);
      }
   }

   fmt_predefined_headwords_before ();

   while (fgets(buf = buffer,BSIZE-1,stdin)) {
      if (strlen(buffer))
	 buffer[strlen(buffer)-1] = '\0'; /* remove newline */
      
      switch (type) {
      case HITCHCOCK:
	 if (strlen(buffer) == 1) {
	    header = 1;
	    continue;;
	 }
	 if (header) {
	    strcpy( buffer2, buffer );
	    if ((pt = strchr( buffer2, ','))) {
	       *pt = '\0';
	       fmt_newheadword(buffer2);
	       *pt = ',';

//	       fprintf (stderr, "HW=`%s`\n", buffer2);
//	       if (*pt == '\n')
//		  ++pt;
//	       fprintf (stderr, "DEF=`%s`\n", pt);

//	       buf = pt;
	    }
	 }
	 break;
      case EASTON:
	 strcpy( buffer2, buffer );
	 for (s = buffer2, d = buffer; *s; ++s) {
	    if (*s == '<') {
	       header = 1;
	       switch (s[1]) {
	       case 'I': *d++ = '_'; break;
	       case 'A':
		  if (s[3] == 'N') goto skip;
		  *d++ = '{';
		  break;
	       case 'P': goto skip;
	       case 'B': goto copy;
	       case '/':
		  switch(s[2]) {
		  case 'I': *d++ = '_'; break;
		  case 'A': *d++ = '}'; break;
		  case 'B': goto copy;
		  default:
		     fprintf( stderr,
			      "Unknown tag: %s (%c%c)\n",
			      buffer2, s[1], s[2] );

		     destroy_and_exit (1);
		  }
		  break;
	       default:
		  fprintf( stderr, "Unknown tag: %s (%c)\n", buffer2, s[1] );

		  destroy_and_exit (1);
	       }
	       while (*s && *s != '>') s++;
	       continue;
	    }
      copy:
	    *d++ = *s;
	 }
	 *d = '\0';
#if 0
	 printf( "BEFORE: %s\n", buffer2 );
	 printf( "AFTER: %s\n", buffer );
#endif

	 if (*buffer == '<') {
	    switch (buffer[1]) {
	    case 'B':
	       if ((pt = strstr( buffer+3, " - </B>" ))) {
		  *pt = '\0';
		  fmt_newheadword(buffer+3);
		  continue;
	       } else {
		  fprintf( stderr, "No end: %s\n", buffer );

		  destroy_and_exit (1);
	       }
	       break;
	    default:
	       fprintf( stderr, "Unknown: %s\n", buffer );

	       destroy_and_exit (1);
	    }
	 } else {
	    if (buffer[0] == ' ' && buffer[1] == ' ') fmt_newline();
	 }
	 break;
      case JARGON:
	 switch (*buffer) {
	 case ':':
	    header = 1;
	    if ((pt = strchr( buffer+1, ':' ))) {
	       s = pt + 1;
	       if (*s == ':') ++s;
	       
	       *pt = '\0';
	       fmt_newheadword (buffer+1);
	       memmove( buf, s, strlen(s)+1 ); /* move \0 also */
	    }
	    break;
	 case '*':
	 case '=':
	 case '-':
	    if (buffer[0] == buffer[1]
		&& buffer[0] == buffer[2]
		&& buffer[0] == buffer[3])
	       continue;		/* Skip lines with *'s and ='s */
	 }
	 break;
      case PERIODIC:
	 switch (*buffer) {
	 case '%':
	    if (buffer[1] == 'h') {
	       if (!header) {
		  header = 1;
		  continue;
	       } else {
		  fmt_newheadword(buffer+3);
		  continue;
	       }
	    } else if (buffer[1] == 'd') {
	       continue;
	    }	    
	    break;
	 }
	 break;
      case VERA:
          switch (*buffer) {
          case '@':
              if (header && !strncmp(buffer, "@item ", 6)) {
		 fmt_newheadword(buffer+6);
              }
              continue;
          }
          if (!header) {
              fmt_string("This is a special GNU edition of V.E.R.A.,");
              fmt_string("a list dealing with computational acronyms.");
              fmt_newline();
              fmt_string("Copyright 1993/2002 Oliver Heidelbach <ohei [at] snafu de>");
              fmt_newline();
              fmt_newline();
              fmt_string(
"Permission is granted to copy, distribute and/or modify this document"
" under the terms of the GNU Free Documentation License, Version 1.1"
" or any later version published by the Free Software Foundation;"
" with no Invariant Sections, with no Front-Cover Texts, and with "
" no Back-Cover Texts.");
              fmt_newline();
              fmt_newline();
              fmt_string(
"Within the above restrictions the distribution of this"
" document is explicitly encouraged and I hope you'll find"
" it of some value.");
              fmt_newline();
              fmt_newline();
              fmt_string(
"This dictionary has nothing to do with Systems Science Inc. "
"or its products.");
              fmt_newline();
              ++header;
          }
          break;
      case FOLDOC:
	 ++header;
	 if (header < 3){
	    if (without_info)
	       continue;
	 }else{
	    if (*buffer && *buffer != ' ' && *buffer != '\t') {
	       fmt_newheadword(buffer);
	       continue;
	    }
	 }
	 if (*buf == '\t') {
	    memmove( buf+2, buf, strlen(buf)+1 ); /* move \0 */
	    buf[0] = buf[1] = buf[2] = ' ';
	 }
	 break;
      case CIA1995:
	 if (*buffer == '@') {
	    buf++;
	 } else if (strncmp(buffer, "_____",5) == 0) {
	    fgets(buf = buffer,BSIZE-1,stdin); /* empty line */
	    
	    fgets(buf = buffer,BSIZE-1,stdin);
	    if (strlen(buffer))
	       buffer[strlen(buffer)-1] = '\0'; /* remove newline */

	    buf = trim_left (buf);

	    if (*buf != '\0') {
	       fmt_indent = 0;
	       fmt_newheadword (buf);
	       continue;
	    }
 	 }
 	 break;
      default:
	 fprintf(stderr, "Unknown input format type %d\n", type );

	 destroy_and_exit (2);
      }
      if (buf){
	 fmt_string(buf);
	 fmt_indent = 0;
	 fmt_newline();
//	 fmt_indent = FMT_INDENT;
      }
   skip:;
   }

   fmt_predefined_headwords_after ();

   fmt_closeindex();
   fclose(str);

   destroy ();

   return 0;
}

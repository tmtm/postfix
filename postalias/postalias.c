/*++
/* NAME
/*	postalias 1
/* SUMMARY
/*	Postfix alias database maintenance
/* SYNOPSIS
/* .fi
/*	\fBpostalias\fR [\fB-c \fIconfig_dir\fR] [\fB-i\fR] [\fB-v\fR]
/*		[\fIfile_type\fR:]\fIfile_name\fR ...
/* DESCRIPTION
/*	The \fBpostalias\fR command creates a new Postfix alias database,
/*	or updates an existing one. The input and output file formats
/*	are expected to be compatible with Sendmail version 8, and are
/*	expected to be suitable for the use as NIS alias maps.
/*
/*	While a database update is in progress, signal delivery is
/*	postponed, and an exclusive, advisory, lock is placed on the
/*	entire database, in order to avoid surprises in spectator
/*	programs.
/*
/*	Options:
/* .IP "\fB-c \fIconfig_dir\fR"
/*	Read the \fBmain.cf\fR configuration file in the named directory.
/* .IP \fB-i\fR
/*	Incremental mode. Read entries from standard input and do not
/*	truncate an existing database. By default, \fBpostalias\fR creates
/*	a new database from the entries in \fBfile_name\fR.
/* .IP \fB-v\fR
/*	Enable verbose logging for debugging purposes. Multiple \fB-v\fR
/*	options make the software increasingly verbose.
/* .PP
/*	Arguments:
/* .IP \fIfile_type\fR
/*	The type of database to be produced.
/* .RS
/* .IP \fBbtree\fR
/*	The output is a btree file, named \fIfile_name\fB.db\fR.
/*	This is available only on systems with support for \fBdb\fR databases.
/* .IP \fBdbm\fR
/*	The output consists of two files, named \fIfile_name\fB.pag\fR and
/*	\fIfile_name\fB.dir\fR.
/*	This is available only on systems with support for \fBdbm\fR databases.
/* .IP \fBhash\fR
/*	The output is a hashed file, named \fIfile_name\fB.db\fR.
/*	This is available only on systems with support for \fBdb\fR databases.
/* .PP
/*	When no \fIfile_type\fR is specified, the software uses the database
/*	type specified via the \fBdatabase_type\fR configuration parameter.
/*	The default value for this parameter depends on the host environment.
/* .RE
/* .IP \fIfile_name\fR
/*	The name of the alias database source file when rebuilding a database.
/* DIAGNOSTICS
/*	Problems are logged to the standard error stream. No output means
/*	no problems were detected. Duplicate entries are skipped and are
/*	flagged with a warning.
/* ENVIRONMENT
/* .ad
/* .fi
/* .IP \fBMAIL_CONFIG\fR
/*	Mail configuration database.
/* .IP \fBMAIL_VERBOSE\fR
/*	Enable verbose logging for debugging purposes.
/* CONFIGURATION PARAMETERS
/* .ad
/* .fi
/*	The following \fBmain.cf\fR parameters are especially relevant to
/*	this program. See the Postfix \fBmain.cf\fR file for syntax details
/*	and for default values.
/* .IP \fBdatabase_type\fR
/*	Default alias database type. On many UNIX systems, the default type
/*	is either \fBdbm\fR or \fBhash\fR.
/* STANDARDS
/*	RFC 822 (ARPA Internet Text Messages)
/* SEE ALSO
/*	aliases(5) format of alias database input file.
/*	sendmail(1) mail posting and compatibility interface.
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>

/* Utility library. */

#include <msg.h>
#include <mymalloc.h>
#include <vstring.h>
#include <vstream.h>
#include <msg_vstream.h>
#include <readline.h>
#include <stringops.h>
#include <split_at.h>

/* Global library. */

#include <tok822.h>
#include <config.h>
#include <mail_params.h>
#include <mkmap.h>

/* Application-specific. */

#define STR	vstring_str

/* postalias - create or update alias database */

static void postalias(char *map_type, char *path_name, int incremental)
{
    VSTREAM *source_fp;
    VSTRING *line_buffer;
    MKMAP  *mkmap;
    int     lineno;
    VSTRING *key_buffer;
    VSTRING *value_buffer;
    TOK822 *tok_list;
    TOK822 *key_list;
    TOK822 *colon;
    TOK822 *value_list;

    /*
     * Initialize.
     */
    line_buffer = vstring_alloc(100);
    key_buffer = vstring_alloc(100);
    value_buffer = vstring_alloc(100);
    if (incremental) {
	source_fp = VSTREAM_IN;
	vstream_control(source_fp, VSTREAM_CTL_PATH, "stdin", VSTREAM_CTL_END);
    } else if ((source_fp = vstream_fopen(path_name, O_RDONLY, 0)) == 0) {
	msg_fatal("open %s: %m", path_name);
    }

    /*
     * Open the database, create it when it does not exist, truncate it when
     * it does exist, and lock out any spectators.
     */
    mkmap = mkmap_open(map_type, path_name, incremental ?
		       O_RDWR | O_CREAT : O_RDWR | O_CREAT | O_TRUNC);

    /*
     * Add records to the database.
     */
    lineno = 0;
    while (readline(line_buffer, source_fp, &lineno)) {

	/*
	 * Skip comments.
	 */
	if (*STR(line_buffer) == '#')
	    continue;

	/*
	 * Weird stuff. Normally, a line that begins with whitespace is a
	 * continuation of the previous line.
	 */
	if (ISSPACE(*STR(line_buffer))) {
	    msg_warn("%s, line %d: malformed line",
		     VSTREAM_PATH(source_fp), lineno);
	    continue;
	}

	/*
	 * Tokenize the input, so that we do the right thing when a quoted
	 * localpart contains special characters such as "@", ":" and so on.
	 */
	if ((tok_list = tok822_scan(STR(line_buffer), (TOK822 **) 0)) == 0)
	    continue;

	/*
	 * Enforce the key:value format. Disallow missing keys, multi-address
	 * keys, or missing values. In order to specify an empty string or
	 * value, enclose it in double quotes.
	 */
	if ((colon = tok822_find_type(tok_list, ':')) == 0
	    || colon->prev == 0 || colon->next == 0
	    || tok822_rfind_type(colon, ',')) {
	    msg_warn("%s, line %d: need name:value pair",
		     VSTREAM_PATH(source_fp), lineno);
	    tok822_free_tree(tok_list);
	    continue;
	}

	/*
	 * Key must be local. XXX We should use the Postfix rewriting and
	 * resolving services to handle all address forms correctly. However,
	 * we can't count on the mail system being up when the alias database
	 * is being built, so we're guessing a bit.
	 */
	if (tok822_rfind_type(colon, '@') || tok822_rfind_type(colon, '%')) {
	    msg_warn("%s, line %d: name must be local",
		     VSTREAM_PATH(source_fp), lineno);
	    tok822_free_tree(tok_list);
	    continue;
	}

	/*
	 * Split the input into key and value parts, and convert from token
	 * representation back to string representation. Convert the key to
	 * internal (unquoted) form, because the resolver produces addresses
	 * in internal form. Convert the value to external (quoted) form,
	 * because it will have to be re-parsed upon lookup. Discard the
	 * token representation when done.
	 */
	key_list = tok_list;
	tok_list = 0;
	value_list = tok822_cut_after(colon);
	tok822_unlink(colon);
	tok822_free(colon);

	tok822_internalize(key_buffer, key_list, TOK822_STR_DEFL);
	tok822_free_tree(key_list);

	tok822_externalize(value_buffer, value_list, TOK822_STR_DEFL);
	tok822_free_tree(value_list);

	/*
	 * Store the value under a case-insensitive key.
	 */
	lowercase(STR(key_buffer));
	mkmap_append(mkmap, STR(key_buffer), STR(value_buffer));
    }

    /*
     * Sendmail compatibility: add the @:@ signature to indicate that the
     * database is complete. This might be needed by NIS clients running
     * sendmail.
     */
    mkmap_append(mkmap, "@", "@");

    /*
     * Close the alias database, and release the lock.
     */
    mkmap_close(mkmap);

    /*
     * Cleanup. We're about to terminate, but it is a good sanity check.
     */
    vstring_free(value_buffer);
    vstring_free(key_buffer);
    vstring_free(line_buffer);
    if (source_fp != VSTREAM_IN)
	vstream_fclose(source_fp);
}

/* usage - explain */

static NORETURN usage(char *myname)
{
    msg_fatal("usage: %s [-c config_directory] [-i] [-v] [output_type:]file...",
	      myname);
}

int     main(int argc, char **argv)
{
    char   *path_name;
    int     ch;
    int     fd;
    char   *slash;
    struct stat st;
    int     incremental = 0;

    /*
     * Be consistent with file permissions.
     */
    umask(022);

    /*
     * To minimize confusion, make sure that the standard file descriptors
     * are open before opening anything else. XXX Work around for 44BSD where
     * fstat can return EBADF on an open file descriptor.
     */
    for (fd = 0; fd < 3; fd++)
	if (fstat(fd, &st) == -1
	    && (close(fd), open("/dev/null", 2)) != fd)
	    msg_fatal("open /dev/null: %m");

    /*
     * Process environment options as early as we can. We are not set-uid,
     * and we are supposed to be running in a controlled environment.
     */
    if (getenv(CONF_ENV_VERB))
	msg_verbose = 1;

    /*
     * Initialize. Set up logging, read the global configuration file and
     * extract configuration information.
     */
    if ((slash = strrchr(argv[0], '/')) != 0)
	argv[0] = slash + 1;
    msg_vstream_init(argv[0], VSTREAM_ERR);

    /*
     * Parse JCL.
     */
    while ((ch = GETOPT(argc, argv, "c:iv")) > 0) {
	switch (ch) {
	default:
	    usage(argv[0]);
	    break;
	case 'c':
	    if (setenv(CONF_ENV_PATH, optarg, 1) < 0)
		msg_fatal("out of memory");
	    break;
	case 'i':
	    incremental = 1;
	    break;
	case 'v':
	    msg_verbose++;
	    break;
	}
    }
    read_config();

    /*
     * Use the map type specified by the user, or fall back to a default
     * database type.
     */
    if (optind + 1 > argc)
	usage(argv[0]);
    while (optind < argc) {
	if ((path_name = split_at(argv[optind], ':')) != 0) {
	    postalias(argv[optind], path_name, incremental);
	} else {
	    postalias(var_db_type, argv[optind], incremental);
	}
	optind++;
    }
    exit(0);
}

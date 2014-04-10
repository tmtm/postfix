/*++
/* NAME
/*	dict 3
/* SUMMARY
/*	dictionary manager
/* SYNOPSIS
/*	#include <dict.h>
/*
/*	extern int dict_unknown_allowed;
/*	extern int dict_errno;
/*
/*	void	dict_register(dict_name, dict_info)
/*	const const char *dict_name;
/*	DICT	*dict_info;
/*
/*	DICT	*dict_handle(dict_name)
/*	const const char *dict_name;
/*
/*	void	dict_unregister(dict_name)
/*	const const char *dict_name;
/*
/*	void	dict_update(dict_name, member, value)
/*	const const char *dict_name;
/*	const const char *member;
/*	const char *value;
/*
/*	const char *dict_lookup(dict_name, member)
/*	const const char *dict_name;
/*	const const char *member;
/*
/*	const char *dict_eval(dict_name, string, int recursive)
/*	const const char *dict_name;
/*	const char *string;
/*	int	recursive;
/* AUXILIARY FUNCTIONS
/*	void	dict_load_file(dict_name, path)
/*	const const char *dict_name;
/*	const const char *path;
/*
/*	void	dict_load_fp(dict_name, fp)
/*	const const char *dict_name;
/*	FILE	*fp;
/* DESCRIPTION
/*	This module maintains a collection of name-value dictionaries.
/*	Each dictionary has its own name and has its own methods to read
/*	or update members. Examples of dictionaries that can be accessed
/*	in this manner are the global UNIX-style process environment,
/*	hash tables, NIS maps, DBM files, and so on. Dictionary values
/*	are not limited to strings but can be arbitrary objects as long
/*	as they can be represented by character pointers.
/* FEATURES
/* .fi
/* .ad
/*	Notable features of this module are:
/* .IP "macro expansion (string-valued dictionaries only)"
/*	Macros of the form $\fIname\fR can be expanded to the current
/*	value of \fIname\fR. The forms $(\fIname\fR) and ${\fIname\fR} are
/*	also supported.
/* .IP "unknown names"
/*	References to unknown dictionary or dictionary member names either
/*	default to an empty dictionary or null pointer value, or cause a
/*	run-time error. The behavior is controlled by the global
/*	\fIdict_unknown_allowed\fR boolean variable.
/* .PP
/*	dict_register() adds a new dictionary, including access methods,
/*	to the list of known dictionaries, or increments the reference
/*	count for an existing (name, dictionary) pair.  Otherwise, it is
/*	an error to pass an existing name (this would cause a memory leak).
/*
/*	dict_handle() returns the generic dictionary handle of the
/*	named dictionary, or a null pointer when the named dictionary
/*	is not found.
/*
/*	dict_unregister() decrements the reference count of the named
/*	dictionary. When the reference count reaches zero, dict_unregister()
/*	breaks the (name, dictionary) association and executes the
/*	dictionary's optional \fIremove\fR method.
/*
/*	dict_update() updates the value of the named dictionary member.
/*	The dictionary member and the named dictionary are instantiated
/*	on the fly.  During the update, a file-based dictionary is locked
/*	for exclusive access. With file-based dictionaries, duplicate
/*	of duplicate entries depends on dictionary flag settings:
/* .IP DICT_FLAG_DUP_WARN
/*	Log a warning and ignore the duplicate.
/* .IP DICT_FLAG_DUP_IGNORE
/*	Silently ignore the duplicate.
/* .PP
/*	The default is to terminate the program with a fatal error.
/*
/*	dict_lookup() returns the value of the named member (i.e. without
/*	expanding macros in the member value).  The \fIdict_name\fR argument
/*	specifies the dictionary to search. The dictionary is locked for
/*	shared access, when it is file based.  The result is a null pointer
/*	when no value is found, otherwise the result is owned by the
/*	underlying dictionary method. Make a copy if the result is to be
/*	modified, or if the result is to survive multiple dict_lookup() calls.
/*
/*	dict_eval() expands macro references in the specified string.
/*	The result is owned by the dictionary manager. Make a copy if the
/*	result is to survive multiple dict_eval() calls. When the
/*	\fIrecursive\fR argument is non-zero, macros references are
/*	expanded recursively.
/*
/*	dict_load_file() reads name-value entries from the named file.
/*	Lines that begin with whitespace are concatenated to the preceding
/*	line (the newline is deleted).
/*	Each entry is stored in the dictionary named by \fIdict_name\fR.
/*
/*	dict_load_fp() reads name-value entries from an open stream.
/*	It has the same semantics as the dict_load_file() function.
/* SEE ALSO
/*	htable(3)
/* BUGS
/* DIAGNOSTICS
/*	Fatal errors: out of memory, reference to unknown name,
/*	malformed macro name.
/*
/*	The lookup routines may set the \fIdict_errno\fR variable when
/*	they were unable to find the requested result. The variable
/*	is reset before each lookup operation. \fIdict_errno\fR can
/*	have the following values:
/* .IP DICT_ERR_RETRY
/*	The dictionary was temporarily unavailable. This can happen
/*	with network-based services.
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

/* System libraries. */

#include "sys_defs.h"
#include <fcntl.h>
#include <ctype.h>
#include <string.h>

/* Utility library. */

#include "msg.h"
#include "htable.h"
#include "mymalloc.h"
#include "vstream.h"
#include "vstring.h"
#include "readline.h"
#include "myflock.h"
#include "mac_parse.h"
#include "dict.h"
#include "dict_ht.h"

 /*
  * By default, use a sane default for an unknown name.
  */
int     dict_unknown_allowed = 1;
int     dict_errno = 0;

static HTABLE *dict_table;

 /*
  * Each (name, dictionary) instance has a reference count. The count is part
  * of the name, not the dictionary. The same dictionary may be registered
  * under multiple names. The structure below keeps track of instances and
  * reference counts.
  */
typedef struct {
    DICT   *dict;
    int     refcount;
} DICT_NODE;

#define dict_node(dict) \
	(dict_table ? (DICT_NODE *) htable_find(dict_table, dict) : 0)

#define STR(x)	vstring_str(x)

/* dict_register - make association with dictionary */

void    dict_register(const char *dict_name, DICT *dict_info)
{
    char   *myname = "dict_register";
    DICT_NODE *node;

    if (dict_table == 0)
	dict_table = htable_create(0);
    if ((node = dict_node(dict_name)) == 0) {
	node = (DICT_NODE *) mymalloc(sizeof(*node));
	node->dict = dict_info;
	node->refcount = 0;
	htable_enter(dict_table, dict_name, (char *) node);
    } else if (dict_info != node->dict)
	msg_fatal("%s: dictionary name exists: %s", myname, dict_name);
    node->refcount++;
    if (msg_verbose > 1)
	msg_info("%s: %s %d", myname, dict_name, node->refcount);
}

/* dict_handle - locate generic dictionary handle */

DICT   *dict_handle(const char *dict_name)
{
    DICT_NODE *node;

    return ((node = dict_node(dict_name)) ? node->dict : 0);
}

/* dict_node_free - dict_unregister() callback */

static void dict_node_free(char *ptr)
{
    DICT_NODE *node = (DICT_NODE *) ptr;
    DICT   *dict = node->dict;

    if (dict->close)
	dict->close(dict);
    myfree((char *) node);
}

/* dict_unregister - break association with named dictionary */

void    dict_unregister(const char *dict_name)
{
    char   *myname = "dict_unregister";
    DICT_NODE *node;

    if ((node = dict_node(dict_name)) == 0)
	msg_panic("non-existing dictionary: %s", dict_name);
    if (msg_verbose > 1)
	msg_info("%s: %s %d", myname, dict_name, node->refcount);
    if (--(node->refcount) == 0)
	htable_delete(dict_table, dict_name, dict_node_free);
}

/* dict_update - replace or add dictionary entry */

void    dict_update(const char *dict_name, const char *member, const char *value)
{
    char   *myname = "dict_update";
    DICT_NODE *node;
    DICT   *dict;

    if ((node = dict_node(dict_name)) == 0) {
	if (dict_unknown_allowed == 0)
	    msg_fatal("%s: unknown dictionary: %s", myname, dict_name);
	dict = dict_ht_open(htable_create(0), myfree);
	dict_register(dict_name, dict);
    } else
	dict = node->dict;
    if (msg_verbose > 1)
	msg_info("%s: %s = %s", myname, member, value);
    if (dict->fd >= 0 && myflock(dict->fd, MYFLOCK_EXCLUSIVE) < 0)
	msg_fatal("%s: lock dictionary %s: %m", myname, dict_name);
    dict->update(dict, member, value);
    if (dict->fd >= 0 && myflock(dict->fd, MYFLOCK_NONE) < 0)
	msg_fatal("%s: unlock dictionary %s: %m", myname, dict_name);
}

/* dict_lookup - look up dictionary entry */

const char *dict_lookup(const char *dict_name, const char *member)
{
    char   *myname = "dict_lookup";
    DICT_NODE *node;
    DICT   *dict;
    const char *ret = 0;

    dict_errno = 0;

    if ((node = dict_node(dict_name)) == 0) {
	if (dict_unknown_allowed == 0)
	    msg_fatal("%s: unknown dictionary: %s", myname, dict_name);
    } else {
	dict = node->dict;
	if (dict->fd >= 0 && myflock(dict->fd, MYFLOCK_SHARED) < 0)
	    msg_fatal("%s: lock dictionary %s: %m", myname, dict_name);
	ret = dict->lookup(dict, member);
	if (dict->fd >= 0 && myflock(dict->fd, MYFLOCK_NONE) < 0)
	    msg_fatal("%s: unlock dictionary %s: %m", myname, dict_name);
	if (ret == 0 && dict_unknown_allowed == 0)
	    msg_fatal("dictionary %s: unknown member: %s", dict_name, member);
    }
    if (msg_verbose > 1)
	msg_info("%s: %s = %s", myname, member, ret ? ret : "(notfound)");
    return (ret);
}

/* dict_load_file - read entries from text file */

void    dict_load_file(const char *dict_name, const char *path)
{
    VSTREAM *fp;

    if ((fp = vstream_fopen(path, O_RDONLY, 0)) == 0)
	msg_fatal("open %s: %m", path);
    dict_load_fp(dict_name, fp);
    if (vstream_ferror(fp) || vstream_fclose(fp))
	msg_fatal("read %s: %m", path);
}

/* dict_load_fp - read entries from open stream */

void    dict_load_fp(const char *dict_name, VSTREAM *fp)
{
    VSTRING *buf;
    char   *start;
    char   *member;
    char   *val;
    char   *cp;
    char   *ep;
    int     lineno;

    /*
     * Ugly macros to make complex expressions less unreadable.
     */
#define SKIP(start, var, cond) \
	for (var = start; *var && (cond); var++);

#define TRIM(s) { \
	char *p; \
	for (p = (s) + strlen(s); p > (s) && ISSPACE(p[-1]); p--); \
	*p = 0; \
    }

    buf = vstring_alloc(100);
    lineno = 0;

    while (readline(buf, fp, &lineno)) {
	start = STR(buf);
	SKIP(start, member, ISSPACE(*member));	/* find member begin */
	if (*member == 0 || *member == '#')
	    continue;				/* comment or blank line */
	SKIP(member, ep, !ISSPACE(*ep) && *ep != '=');	/* find member end */
	SKIP(ep, cp, ISSPACE(*cp));		/* skip blanks before '=' */
	if (*cp && *cp != '=')			/* need '=' or end of string */
	    msg_fatal("%s, line %d: whitespace in attribute name: \"%s\"",
		      VSTREAM_PATH(fp), lineno, member);
	if (*cp)
	    cp++;				/* skip over '=' */
	*ep = 0;				/* terminate member name */
	SKIP(cp, val, ISSPACE(*val));		/* skip leading blanks */
	TRIM(val);				/* trim trailing blanks */
	dict_update(dict_name, member, val);
    }
    vstring_free(buf);
}

 /*
  * Helper for macro expansion callback.
  */
struct dict_eval_context {
    const char *dict_name;		/* where to look */
    VSTRING *buf;			/* result buffer */
    int     recursive;			/* recursive or not */
};

/* dict_eval_action - macro parser call-back routine */

static void dict_eval_action(int type, VSTRING *buf, char *ptr)
{
    struct dict_eval_context *ctxt = (struct dict_eval_context *) ptr;
    char   *myname = "dict_eval_action";
    const char *pp;

    if (msg_verbose)
	msg_info("%s: type %s buf %s context %s \"%s\" %s",
		 myname, type == MAC_PARSE_VARNAME ? "variable" : "literal",
		 STR(buf), ctxt->dict_name, STR(ctxt->buf),
		 ctxt->recursive ? "recursive" : "non-recursive");

    /*
     * In order to support recursion, we must save the dict_lookup() result.
     * We use the input buffer since it will not be needed anymore.
     */
    if (type == MAC_PARSE_VARNAME) {
	if ((pp = dict_lookup(ctxt->dict_name, STR(buf))) == 0) {
	    if (dict_errno)			/* XXX how would one recover? */
		msg_fatal("dictionary %s: lookup %s: temporary error",
			  ctxt->dict_name, STR(buf));
	} else if (ctxt->recursive) {
	    vstring_strcpy(buf, pp);		/* XXX clobber input */
	    dict_eval(ctxt->dict_name, STR(buf), ctxt->recursive);
	} else {
	    vstring_strcat(ctxt->buf, pp);
	}
    } else {
	vstring_strcat(ctxt->buf, STR(buf));
    }
}

/* dict_eval - expand embedded dictionary references */

const char *dict_eval(const char *dict_name, const char *value, int recursive)
{
    static VSTRING *buf;
    static struct dict_eval_context ctxt;
    static int loop = 0;

    /*
     * Sanity check.
     */
    if (loop > 100)
	msg_fatal("unreasonable macro nesting: \"%s\"", value);

    /*
     * Initialize.
     */
    if (buf == 0)
	buf = vstring_alloc(10);
    if (loop++ == 0)
	VSTRING_RESET(buf);
    ctxt.buf = buf;
    ctxt.recursive = recursive;
    ctxt.dict_name = dict_name;

    /*
     * Expand macros, possibly recursively.
     */
    if (msg_verbose > 1)
	msg_info("dict_eval[%d] %s", loop, value);

    mac_parse(value, dict_eval_action, (char *) &ctxt);

    if (msg_verbose > 1)
	msg_info("dict_eval[%d] result %s", loop, STR(buf));

    /*
     * Cleanup.
     */
    loop--;
    VSTRING_TERMINATE(buf);

    return (STR(buf));
}

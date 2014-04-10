/*++
/* NAME
/*	config_str 3
/* SUMMARY
/*	string-valued global configuration parameter support
/* SYNOPSIS
/*	#include <config.h>
/*
/*	char	*get_config_str(name, defval, min, max)
/*	const char *name;
/*	const char *defval;
/*	int	min;
/*	int	max;
/*
/*	char	*get_config_str_fn(name, defval, min, max)
/*	const char *name;
/*	const char *(*defval)(void);
/*	int	min;
/*	int	max;
/*
/*	void	set_config_str(name, value)
/*	const char *name;
/*	const char *value;
/*
/*	void	get_config_str_table(table)
/*	CONFIG_STR_TABLE *table;
/*
/*	void	get_config_str_fn_table(table)
/*	CONFIG_STR_TABLE *table;
/* DESCRIPTION
/*	This module implements support for string-valued global
/*	configuration parameters.
/*
/*	get_config_str() looks up the named entry in the global
/*	configuration dictionary. The default value is returned when
/*	no value was found. String results should be passed to myfree()
/*	when no longer needed.  \fImin\fR is zero or specifies a lower
/*	bound on the string length; \fImax\fR is zero or specifies an
/*	upper limit on the string length.
/*
/*	get_config_str_fn() is similar but specifies a function that
/*	provides the default value. The function is called only when
/*	the default value is used.
/*
/*	set_config_str() updates the named entry in the global
/*	configuration dictionary. This has no effect on values that
/*	have been looked up earlier via the get_config_XXX() routines.
/*
/*	get_config_str_table() and get_config_str_fn_table() read
/*	lists of variables, as directed by their table arguments. A table
/*	must be terminated by a null entry.
/* DIAGNOSTICS
/*	Fatal errors: bad string length.
/* SEE ALSO
/*	config(3) generic config parameter support
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
#include <stdlib.h>
#include <string.h>

/* Utility library. */

#include <msg.h>
#include <mymalloc.h>

/* Global library. */

#include "config.h"

/* check_config_str - validate string length */

static void check_config_str(const char *name, const char *strval,
			             int min, int max)
{
    int     len = strlen(strval);

    if (min && len < min)
	msg_fatal("bad string length (%d < %d): %s = %s",
		  len, min, name, strval);
    if (max && len > max)
	msg_fatal("bad string length (%d > %d): %s = %s",
		  len, max, name, strval);
}

/* get_config_str - evaluate string-valued configuration variable */

char   *get_config_str(const char *name, const char *defval,
		               int min, int max)
{
    const char *strval;

    if ((strval = config_lookup_eval(name)) == 0) {
	strval = config_eval(defval);
	config_update(name, strval);
    }
    check_config_str(name, strval, min, max);
    return (mystrdup(strval));
}

/* get_config_str_fn - evaluate string-valued configuration variable */

typedef const char *(*stupid_indent_str) (void);

char   *get_config_str_fn(const char *name, stupid_indent_str defval,
			          int min, int max)
{
    const char *strval;

    if ((strval = config_lookup_eval(name)) == 0) {
	strval = config_eval(defval());
	config_update(name, strval);
    }
    check_config_str(name, strval, min, max);
    return (mystrdup(strval));
}

/* set_config_str - update string-valued configuration dictionary entry */

void    set_config_str(const char *name, const char *value)
{
    config_update(name, value);
}

/* get_config_str_table - look up table of strings */

void    get_config_str_table(CONFIG_STR_TABLE *table)
{
    while (table->name) {
	if (table->target[0])
	    myfree(table->target[0]);
	table->target[0] = get_config_str(table->name, table->defval,
					  table->min, table->max);
	table++;
    }
}

/* get_config_str_fn_table - look up strings, defaults are functions */

void    get_config_str_fn_table(CONFIG_STR_FN_TABLE *table)
{
    while (table->name) {
	if (table->target[0])
	    myfree(table->target[0]);
	table->target[0] = get_config_str_fn(table->name, table->defval,
					     table->min, table->max);
	table++;
    }
}

/*++
/* NAME
/*	smtpd 3h
/* SUMMARY
/*	smtp server
/* SYNOPSIS
/*	include "smtpd.h"
/* DESCRIPTION
/* .nf

 /*
  * Utility library.
  */
#include <vstream.h>
#include <vstring.h>
#include <argv.h>

 /*
  * Global library.
  */
#include <mail_stream.h>

 /*
  * Variables that keep track of conversation state. There is only one SMTP
  * conversation at a time, so the state variables can be made global. And
  * some of this has to be global anyway, so that the run-time error handler
  * can clean up in case of a fatal error deep down in some library routine.
  */
typedef struct SMTPD_STATE {
    int     err;
    VSTREAM *client;
    VSTRING *buffer;
    time_t  time;
    char   *name;
    char   *addr;
    char   *namaddr;
    int     peer_code;			/* 2=ok, 4=soft, 5=hard */
    int     error_count;
    int     error_mask;
    int     notify_mask;
    char   *helo_name;
    char   *queue_id;
    VSTREAM *cleanup;
    MAIL_STREAM *dest;
    int     rcpt_count;
    char   *access_denied;
    ARGV   *history;
    char   *reason;
    char   *sender;
    char   *recipient;
    char   *etrn_name;
    char   *protocol;
    char   *where;
    int     recursion;
} SMTPD_STATE;

extern void smtpd_state_init(SMTPD_STATE *, VSTREAM *);
extern void smtpd_state_reset(SMTPD_STATE *);

 /*
  * Conversation stages.  This is used for "lost connection after XXX"
  * diagnostics.
  */
#define SMTPD_AFTER_CONNECT	"CONNECT"
#define SMTPD_AFTER_DOT		"END-OF-MESSAGE"

 /*
  * If running in stand-alone mode, do not try to talk to Postfix daemons but
  * write to queue file instead.
  */
#define SMTPD_STAND_ALONE(state) \
	(state->client == VSTREAM_IN && getuid() != var_owner_uid)

 /*
  * SMPTD peer information lookup.
  */
void    smtpd_peer_init(SMTPD_STATE *state);
void    smtpd_peer_reset(SMTPD_STATE *state);

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
/*++
/* NAME
/*	smtp_connect 3
/* SUMMARY
/*	connect to SMTP server
/* SYNOPSIS
/*	#include "smtp.h"
/*
/*	SMTP_SESSION *smtp_connect(destination, why)
/*	char	*destination;
/*	VSTRING	*why;
/* AUXILIARY FUNCTIONS
/*	SMTP_SESSION *smtp_connect_domain(name, port, why)
/*	char	*name;
/*	unsigned port;
/*	VSTRING	*why;
/*
/*	SMTP_SESSION *smtp_connect_host(name, port, why)
/*	char	*name;
/*	unsigned port;
/*	VSTRING	*why;
/* DESCRIPTION
/*	This module implements SMTP connection management.
/*
/*	smtp_connect() attempts to establish an SMTP session with a host
/*	that represents the named domain.
/*
/*	The destination is either a host (or domain) name or a numeric
/*	address. Symbolic or numeric service port information may be
/*	appended, separated by a colon (":").
/*
/*	By default, the Internet domain name service is queried for mail
/*	exchanger hosts. Quote the destination with `[' and `]' to
/*	suppress mail exchanger lookups.
/*
/*	Numerical address information should always be quoted with `[]'.
/*
/*	smtp_connect_domain() attempts to make an SMTP connection to
/*	the named host or domain and network port (network byte order).
/*	\fIname\fR is used to look up mail exchanger information via
/*	the Internet domain name system (DNS).
/*	When no mail exchanger is listed for \fIname\fR, the request
/*	is passed to smtp_connect_host().
/*	Otherwise, mail exchanger hosts are tried in order of preference,
/*	until one is found that responds. In order to avoid mailer loops,
/*	the search for mail exchanger hosts stops when a host is found
/*	that has the same preference as the sending machine.
/*
/*	smtp_connect_host() makes an SMTP connection without looking up
/*	mail exchanger information.  The host can be specified as an
/*	Internet network address or as a symbolic host name.
/* DIAGNOSTICS
/*	All routines either return an SMTP_SESSION pointer, or
/*	return a null pointer and set the \fIsmtp_errno\fR
/*	global variable accordingly:
/* .IP SMTP_RETRY
/*	The connection attempt failed, but should be retried later.
/* .IP SMTP_FAIL
/*	The connection attempt failed.
/* .PP
/*	In addition, a textual description of the error is made available
/*	via the \fIwhy\fR argument.
/* SEE ALSO
/*	smtp_proto(3) SMTP client protocol
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#ifdef STRCASECMP_IN_STRINGS_H
#include <strings.h>
#endif

/* Utility library. */

#include <msg.h>
#include <vstream.h>
#include <vstring.h>
#include <split_at.h>
#include <mymalloc.h>
#include <inet_addr_list.h>
#include <iostuff.h>
#include <timed_connect.h>

/* Global library. */

#include <mail_params.h>
#include <own_inet_addr.h>

/* DNS library. */

#include <dns.h>

/* Application-specific. */

#include "smtp.h"
#include "smtp_addr.h"

/* smtp_connect_addr - connect to explicit address */

static SMTP_SESSION *smtp_connect_addr(DNS_RR *addr, unsigned port,
				               VSTRING *why)
{
    char   *myname = "smtp_connect_addr";
    struct sockaddr_in sin;
    int     sock;
    INET_ADDR_LIST *addr_list;
    int     conn_stat;
    int     saved_errno;
    VSTREAM *stream;
    int     ch;
    unsigned long inaddr;

    /*
     * Sanity checks.
     */
    if (addr->data_len > sizeof(sin.sin_addr)) {
	msg_warn("%s: skip address with length %d", myname, addr->data_len);
	smtp_errno = SMTP_RETRY;
	return (0);
    }

    /*
     * Initialize.
     */
    memset((char *) &sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;

    if ((sock = socket(sin.sin_family, SOCK_STREAM, 0)) < 0)
	msg_fatal("%s: socket: %m", myname);

    /*
     * When running as a virtual host, bind to the virtual interface so that
     * the mail appears to come from the "right" machine address.
     */
    addr_list = own_inet_addr_list();
    if (addr_list->used == 1) {
	sin.sin_port = 0;
	memcpy((char *) &sin.sin_addr, addr_list->addrs, sizeof(sin.sin_addr));
	inaddr = ntohl(sin.sin_addr.s_addr);
	if (!IN_CLASSA(inaddr)
	    || !(((inaddr & IN_CLASSA_NET) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)) {
	    if (bind(sock, (struct sockaddr *) & sin, sizeof(sin)) < 0)
		msg_warn("%s: bind %s: %m", myname, inet_ntoa(sin.sin_addr));
	    if (msg_verbose)
		msg_info("%s: bind %s", myname, inet_ntoa(sin.sin_addr));
	}
    }

    /*
     * Connect to the SMTP server.
     */
    sin.sin_port = port;
    memcpy((char *) &sin.sin_addr, addr->data, sizeof(sin.sin_addr));

    if (msg_verbose)
	msg_info("%s: trying: %s/%s port %d...",
		 myname, addr->name, inet_ntoa(sin.sin_addr), ntohs(port));
    if (var_smtp_conn_tmout > 0) {
	non_blocking(sock, NON_BLOCKING);
	conn_stat = timed_connect(sock, (struct sockaddr *) & sin,
				  sizeof(sin), var_smtp_conn_tmout);
	saved_errno = errno;
	non_blocking(sock, BLOCKING);
	errno = saved_errno;
    } else {
	conn_stat = connect(sock, (struct sockaddr *) & sin, sizeof(sin));
    }
    if (conn_stat < 0) {
	vstring_sprintf(why, "connect to %s: %m", addr->name);
	smtp_errno = SMTP_RETRY;
	close(sock);
	return (0);
    }

    /*
     * Skip this host if it takes no action within some time limit.
     */
    if (read_wait(sock, var_smtp_helo_tmout) < 0) {
	vstring_sprintf(why, "connect to %s: read timeout", addr->name);
	smtp_errno = SMTP_RETRY;
	close(sock);
	return (0);
    }

    /*
     * Skip this host if it disconnects without talking to us.
     */
    stream = vstream_fdopen(sock, O_RDWR);
    if ((ch = VSTREAM_GETC(stream)) == VSTREAM_EOF) {
	vstring_sprintf(why, "connect to %s: server dropped connection",
			addr->name);
	smtp_errno = SMTP_RETRY;
	vstream_fclose(stream);
	return (0);
    }

    /*
     * Skip this host if it sends a 4xx greeting.
     */
    if (ch == '4' && var_smtp_skip_4xx_greeting) {
	vstring_sprintf(why, "connect to %s: server refused mail service",
			addr->name);
	smtp_errno = SMTP_RETRY;
	vstream_fclose(stream);
	return (0);
    }
    vstream_ungetc(stream, ch);
    return (smtp_session_alloc(stream, addr->name, inet_ntoa(sin.sin_addr)));
}

/* smtp_connect_host - direct connection to host */

SMTP_SESSION *smtp_connect_host(char *host, unsigned port, VSTRING *why)
{
    SMTP_SESSION *session = 0;
    DNS_RR *addr_list;
    DNS_RR *addr;

    /*
     * Try each address in the specified order until we find one that works.
     * The addresses belong to the same A record, so we have no information
     * on what address is "best".
     */
    addr_list = smtp_host_addr(host, why);
    for (addr = addr_list; addr; addr = addr->next) {
	if ((session = smtp_connect_addr(addr, port, why)) != 0) {
	    session->best = 1;
	    break;
	}
    }
    dns_rr_free(addr_list);
    return (session);
}

/* smtp_connect_domain - connect to smtp server for domain */

SMTP_SESSION *smtp_connect_domain(char *name, unsigned port, VSTRING *why)
{
    SMTP_SESSION *session = 0;
    DNS_RR *addr_list;
    DNS_RR *addr;

    /*
     * Try each mail exchanger in order of preference until we find one that
     * responds.  Once we find a server that responds we never try
     * alternative mail exchangers. The benefit of this is that we will use
     * backup hosts only when we are unable to reach the primary MX host. If
     * the primary MX host is reachable but does not want to receive our
     * mail, there is no point in trying the backup hosts.
     */
    addr_list = smtp_domain_addr(name, why);
    for (addr = addr_list; addr; addr = addr->next) {
	if ((session = smtp_connect_addr(addr, port, why)) != 0) {
	    session->best = (addr->pref == addr_list->pref);
	    break;
	}
	msg_info("%s; address %s port %d", vstring_str(why),
		 inet_ntoa(*((struct in_addr *) addr->data)), ntohs(port));
    }
    dns_rr_free(addr_list);
    return (session);
}

/* smtp_parse_destination - parse destination */

static char *smtp_parse_destination(char *destination, char *def_service,
				            char **hostp, unsigned *portp)
{
    char   *buf = mystrdup(destination);
    char   *host = buf;
    char   *service;
    struct servent *sp;
    char   *protocol = "tcp";		/* XXX configurable? */
    unsigned port;

    if (msg_verbose)
	msg_info("smtp_parse_destination: %s %s", destination, def_service);

    /*
     * Strip quoting. We're working with a copy of the destination argument
     * so the stripping can be destructive.
     */
    if (*host == '[') {
	host++;
	host[strcspn(host, "]")] = 0;
    }

    /*
     * Separate host and service information, or use the default service
     * specified by the caller. XXX the ":" character is used in the IPV6
     * address notation, so using split_at_right() is not sufficient. We'd
     * have to count the number of ":" instances.
     */
    if ((service = split_at_right(host, ':')) == 0)
	service = def_service;
    if (*service == 0)
	msg_fatal("empty service name: %s", destination);
    *hostp = host;

    /*
     * Convert service to port number, network byte order.
     */
    if ((port = atoi(service)) != 0) {
	*portp = htons(port);
    } else {
	if ((sp = getservbyname(service, protocol)) == 0)
	    msg_fatal("unknown service: %s/%s", service, protocol);
	*portp = sp->s_port;
    }
    return (buf);
}

/* smtp_connect - establish SMTP connection */

SMTP_SESSION *smtp_connect(char *destination, VSTRING *why)
{
    SMTP_SESSION *session;
    char   *dest_buf;
    char   *host;
    unsigned port;
    char   *def_service = "smtp";	/* XXX configurable? */

    /*
     * Parse the destination specification. Default is to use the SMTP port.
     */
    dest_buf = smtp_parse_destination(destination, def_service, &host, &port);

    /*
     * Connect to an SMTP server. Skip mail exchanger lookups when a quoted
     * host is specified, or when DNS lookups are disabled.
     */
    if (msg_verbose)
	msg_info("connecting to %s port %d", host, ntohs(port));
    if (var_disable_dns || *destination == '[') {
	session = smtp_connect_host(host, port, why);
    } else {
	session = smtp_connect_domain(host, port, why);
    }
    myfree(dest_buf);
    return (session);
}

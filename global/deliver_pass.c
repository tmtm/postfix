/*++
/* NAME
/*	deliver_pass 3
/* SUMMARY
/*	deliver request pass_through
/* SYNOPSIS
/*	#include <deliver_request.h>
/*
/*	int	deliver_pass(class, service, request, address, offset)
/*	const char *class;
/*	const char *service;
/*	DELIVER_REQUEST *request;
/*	const char *address;
/*	long	offset;
/* DESCRIPTION
/*	This module implements the client side of the `queue manager
/*	to delivery agent' protocol, passing one recipient on from
/*	one delivery agent to another.
/*
/*	Arguments:
/* .IP class
/*	Destination delivery agent service class
/* .IP service
/*	Destination delivery agent service name.
/* .IP request
/*	Delivery request with queue file information.
/* .IP address
/*	Recipient envelope address.
/* .IP offset
/*	Recipient offset in queue file.
/* DIAGNOSTICS
/* LICENSE
/* .ad
/* .fi
/*	The Secure Mailer license must be distributed with this software.
/* BUGS
/*	One recipient at a time; this is OK for mailbox deliveries.
/*
/*	Hop status information cannot be passed back.
/* AUTHOR(S)
/*	Wietse Venema
/*	IBM T.J. Watson Research
/*	P.O. Box 704
/*	Yorktown Heights, NY 10598, USA
/*--*/

/* System library. */

#include <sys_defs.h>

/* Utility library. */

#include <msg.h>
#include <vstring.h>
#include <vstream.h>

/* Global library. */

#include <mail_proto.h>
#include <deliver_request.h>

/* deliver_pass_initial_reply - retrieve initial delivery process response */

static int deliver_pass_initial_reply(VSTREAM *stream)
{
    int     stat;

    if (mail_scan(stream, "%d", &stat) != 1) {
	msg_warn("%s: malformed response", VSTREAM_PATH(stream));
	stat = -1;
    }
    return (stat);
}

/* deliver_pass_send_request - send delivery request to delivery process */

static int deliver_pass_send_request(VSTREAM *stream, DELIVER_REQUEST *request,
				             const char *addr, long offs)
{
    int     stat;

    mail_print(stream, "%s %s %ld %ld %s %s %s %s %ld %ld %s %s",
	       request->queue_name, request->queue_id,
	       request->data_offset, request->data_size,
	       addr, request->sender,
	       request->errors_to, request->return_receipt,
	       request->arrival_time,
	       offs, addr, "0");

    if (vstream_fflush(stream)) {
	msg_warn("%s: bad write: %m", VSTREAM_PATH(stream));
	stat = -1;
    } else {
	stat = 0;
    }
    return (stat);
}

/* deliver_pass_final_reply - retrieve final delivery status response */

static int deliver_pass_final_reply(VSTREAM *stream, VSTRING *reason)
{
    int     stat;

    if (mail_scan(stream, "%s %d", reason, &stat) != 2) {
	msg_warn("%s: malformed response", VSTREAM_PATH(stream));
	stat = -1;
    }
    return (stat);
}

/* deliver_pass - deliver one per-site queue entry */

int     deliver_pass(const char *class, const char *service,
	              DELIVER_REQUEST *request, const char *addr, long offs)
{
    VSTREAM *stream;
    VSTRING *reason;
    int     status;

    /*
     * Initialize.
     */
    stream = mail_connect_wait(class, service);
    reason = vstring_alloc(1);

    /*
     * Get the delivery process initial response. Send the queue file info
     * and recipient info to the delivery process. Retrieve the delivery
     * agent status report. The numerical status code indicates if delivery
     * should be tried again. The reason text is sent only when a destination
     * should be avoided for a while, so that the queue manager can log why
     * it does not even try to schedule delivery to the affected recipients.
     * XXX Can't pass back hop status info because the problem is with a
     * different transport.
     */
    if ((status = deliver_pass_initial_reply(stream)) == 0
	&& (status = deliver_pass_send_request(stream, request, addr, offs)) == 0)
	status = deliver_pass_final_reply(stream, reason);

    /*
     * Clean up.
     */
    vstream_fclose(stream);
    vstring_free(reason);

    return (status);
}

/*
 * pop3.c -- POP3 protocol methods
 *
 * For license terms, see the file COPYING in this directory.
 */

#include  "config.h"
#ifdef POP3_ENABLE
#include  <stdio.h>
#include  <string.h>
#include  <ctype.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(STDC_HEADERS)
#include  <stdlib.h>
#endif
 
#include  "fetchmail.h"
#include  "socket.h"

#if OPIE
#include <opie.h>
#endif /* OPIE */

#ifndef strstr		/* glibc-2.1 declares this as a macro */
extern char *strstr();	/* needed on sysV68 R3V7.1. */
#endif /* strstr */

static int phase;
#define PHASE_GETAUTH	0
#define PHASE_GETRANGE	1
#define PHASE_GETSIZES	2
#define PHASE_FETCH	3
#define PHASE_LOGOUT	4
static int last;

#if OPIE
static char lastok[POPBUFSIZE+1];
#endif /* OPIE */

int pop3_ok (int sock, char *argbuf)
/* parse command response */
{
    int ok;
    char buf [POPBUFSIZE+1];
    char *bufp;

    if ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
    {
	bufp = buf;
	if (*bufp == '+' || *bufp == '-')
	    bufp++;
	else
	    return(PS_PROTOCOL);

	while (isalpha(*bufp))
	    bufp++;

	if (*bufp)
	  *(bufp++) = '\0';

	if (strcmp(buf,"+OK") == 0)
	{
#if OPIE
	    strcpy(lastok, bufp);
#endif /* OPIE */
	    ok = 0;
	}
	else if (strcmp(buf,"-ERR") == 0)
	{
	    if (phase > PHASE_GETAUTH) 
		ok = PS_PROTOCOL;
	    /*
	     * We're checking for "lock busy", "unable to lock", 
	     * "already locked" etc. here.  This indicates that we
	     * have to wait for the server to clean up before we
	     * can poll again.
	     *
	     * PS_LOCKBUSY check empirically verified with two recent
	     * versions the Berkeley popper;	QPOP (version 2.2)  and
	     * QUALCOMM Pop server derived from UCB (version 2.1.4-R3)
	     */
	    else if (strstr(bufp,"lock")||strstr(bufp,"Lock")||strstr(bufp,"LOCK"))
		ok = PS_LOCKBUSY;
	    else
		ok = PS_AUTHFAIL;
	    if (*bufp)
	      error(0,0,bufp);
	}
	else
	    ok = PS_PROTOCOL;

	if (argbuf != NULL)
	    strcpy(argbuf,bufp);
    }

    return(ok);
}

int pop3_getauth(int sock, struct query *ctl, char *greeting)
/* apply for connection authorization */
{
    int ok;
    char *start,*end;
    char *msg;
#if OPIE
    char *challenge;
#endif /* OPIE */

    phase = PHASE_GETAUTH;

    switch (ctl->server.protocol) {
    case P_POP3:
	 ok = gen_transact(sock, "USER %s", ctl->remotename);

#ifdef RPA_ENABLE
	 /*
	  * CompuServe has changed its RPA behavior.  Used to be they didn't
	  * accept PASS, but I'm told this changed in mid-November 1997.
	  */
	 if (strstr(greeting, "csi.com"))
	 {
             /* temporary fix to get back out of cleartext authentication */
             gen_transact(sock, "PASS %s", "dummypass");
  
	     /* AUTH command should return a list of available mechanisms */
	     if (gen_transact(sock, "AUTH") == 0)
	     {
		 char buffer[10];
		 flag has_rpa = FALSE;

		 while ((ok = gen_recv(sock, buffer, sizeof(buffer))) == 0)
		 {
		     if (buffer[0] == '.')
			 break;
		     if (strncasecmp(buffer, "rpa", 3) == 0)
			 has_rpa = TRUE;
		 }
		 if (has_rpa && !POP3_auth_rpa(ctl->remotename, 
					       ctl->password, sock))
		     return(PS_SUCCESS);
	     }

	     return(PS_AUTHFAIL);
	 }
#endif /* RPA_ENABLE */

#if OPIE
	/* see RFC1938: A One-Time Password System */
	if (challenge = strstr(lastok, "otp-")) {
	  char response[OPIE_RESPONSE_MAX+1];
	  int i;

	  i = opiegenerator(challenge, !strcmp(ctl->password, "opie") ? "" : ctl->password, response);
	  if ((i == -2) && (cmd_daemon == -1)) {
	    char secret[OPIE_SECRET_MAX+1];
	    fprintf(stderr, "Secret pass phrase: ");
	    if (opiereadpass(secret, sizeof(secret), 0))
	      i = opiegenerator(challenge,  secret, response);
	    memset(secret, 0, sizeof(secret));
	  };

	  if (i) {
	    ok = PS_ERROR;
	    break;
	  };

	  ok = gen_transact(sock, "PASS %s", response);
	  break;
	}
#endif /* OPIE */

	/* ordinary validation, no one-time password or RPA */ 
	ok = gen_transact(sock, "PASS %s", ctl->password);
	break;

    case P_APOP:
	/* build MD5 digest from greeting timestamp + password */
	/* find start of timestamp */
	for (start = greeting;  *start != 0 && *start != '<';  start++)
	    continue;
	if (*start == 0) {
	    error(0, -1, "Required APOP timestamp not found in greeting");
	    return(PS_AUTHFAIL);
	}

	/* find end of timestamp */
	for (end = start;  *end != 0  && *end != '>';  end++)
	    continue;
	if (*end == 0 || end == start + 1) {
	    error(0, -1, "Timestamp syntax error in greeting");
	    return(PS_AUTHFAIL);
	}
	else
	    *++end = '\0';

	/* copy timestamp and password into digestion buffer */
	msg = (char *)xmalloc((end-start+1) + strlen(ctl->password) + 1);
	strcpy(msg,start);
	strcat(msg,ctl->password);

	strcpy(ctl->digest, MD5Digest(msg));
	free(msg);

	ok = gen_transact(sock, "APOP %s %s", ctl->remotename, ctl->digest);
	break;

    case P_RPOP:
	if ((ok = gen_transact(sock,"USER %s", ctl->remotename)) == 0)
	    ok = gen_transact(sock, "RPOP %s", ctl->password);
	break;

    default:
	error(0, 0, "Undefined protocol request in POP3_auth");
	ok = PS_ERROR;
    }

    if (ok != 0)
    {
	/* maybe we detected a lock-busy condition? */
        if (ok == PS_LOCKBUSY)
	    error(0, 0, "lock busy!  Is another session active?"); 

	return(ok);
    }

    /*
     * Empirical experience shows some server/OS combinations
     * may need a brief pause even after any lockfiles on the
     * server are released, to give the server time to finish
     * copying back very large mailfolders from the temp-file...
     * this is only ever an issue with extremely large mailboxes.
     */
    sleep(3); /* to be _really_ safe, probably need sleep(5)! */

    /* we're approved */
    return(PS_SUCCESS);
}

static int
pop3_gettopid( int sock, int num , char *id)
{
    int ok;
    int got_it;
    char buf [POPBUFSIZE+1];
    sprintf( buf, "TOP %d 1", num );
    if( (ok = gen_transact(sock, buf ) ) != 0 )
       return ok; 
    got_it = 0;
    while((ok = gen_recv(sock, buf, sizeof(buf))) == 0) {
	if( buf[0] == '.' )
	    break;
	if( ! got_it && ! strncasecmp("Message-Id:", buf, 11 )) {
	    got_it = 1;
	    /* prevent stack overflows */
	    buf[IDLEN+12] = 0;
	    sscanf( buf+12, "%s", id);
	}
    }
    return 0;
}

static int
pop3_slowuidl( int sock,  struct query *ctl, int *countp, int *newp)
{
    /* This approach tries to get the message headers from the
     * remote hosts and compares the message-id to the already known
     * ones:
     *  + if the first message containes a new id, all messages on
     *    the server will be new
     *  + if the first is known, try to estimate the last known message
     *    on the server and check. If this works you know the total number
     *    of messages to get.
     *  + Otherwise run a binary search to determine the last known message
     */
    int ok, nolinear = 0;
    int first_nr, list_len, try_id, try_nr, add_id;
    int num;
    char id [IDLEN+1];
    
    if( (ok = pop3_gettopid( sock, 1, id )) != 0 )
	return ok;
    
    if( ( first_nr = str_nr_in_list(&ctl->oldsaved, id) ) == -1 ) {
	/* the first message is unknown -> all messages are new */
	*newp = *countp;	
	return 0;
    }

    /* check where we expect the latest known message */
    list_len = count_list( &ctl->oldsaved );
    try_id = list_len  - first_nr; /* -1 + 1 */
    if( try_id > 1 ) {
	if( try_id <= *countp ) {
	    if( (ok = pop3_gettopid( sock, try_id, id )) != 0 )
		return ok;
    
	    try_nr = str_nr_last_in_list(&ctl->oldsaved, id);
	} else {
	    try_id = *countp+1;
	    try_nr = -1;
	}
	if( try_nr != list_len -1 ) {
	    /* some messages inbetween have been deleted... */
	    if( try_nr == -1 ) {
		nolinear = 1;

		for( add_id = 1<<30; add_id > try_id-1; add_id >>= 1 )
		    ;
		for( ; add_id; add_id >>= 1 ) {
		    if( try_nr == -1 ) {
			if( try_id - add_id <= 1 ) {
			    continue;
			}
			try_id -= add_id;
		    } else 
			try_id += add_id;
		    
		    if( (ok = pop3_gettopid( sock, try_id, id )) != 0 )
			return ok;
		    try_nr = str_nr_in_list(&ctl->oldsaved, id);
		}
		if( try_nr == -1 ) {
		    try_id--;
		}
	    } else {
		error(0,0,"Messages inserted into list on server. "
		      "Cannot handle this.");
		return -1;
	    }
	} 
    }
    /* the first try_id messages are known -> copy them to the newsaved list */
    for( num = first_nr; num < list_len; num++ )
    {
	struct idlist	*new = save_str(&ctl->newsaved, 
				str_from_nr_list(&ctl->oldsaved, num),
				UID_UNSEEN);
	new->val.status.num = num - first_nr + 1;
    }

    if( nolinear ) {
	free_str_list(&ctl->oldsaved);
	ctl->oldsaved = 0;
	last = try_id;
    }

    *newp = *countp - try_id;
    return 0;
}

static int pop3_getrange(int sock, 
			 struct query *ctl,
			 const char *folder,
			 int *countp, int *newp, int *bytes)
/* get range of messages to be fetched */
{
    int ok;
    char buf [POPBUFSIZE+1];

    phase = PHASE_GETRANGE;

    /* Ensure that the new list is properly empty */
    ctl->newsaved = (struct idlist *)NULL;

#ifdef MBOX
    /* Alain Knaff suggests this, but it's not RFC standard */
    if (folder)
	if ((ok = gen_transact(sock, "MBOX %s", folder)))
	    return ok;
#endif /* MBOX */

    /* get the total message count */
    gen_send(sock, "STAT");
    ok = pop3_ok(sock, buf);
    if (ok == 0)
	sscanf(buf,"%d %d", countp, bytes);
    else
	return(ok);

    /*
     * Newer, RFC-1725-conformant POP servers may not have the LAST command.
     * We work as hard as possible to hide this ugliness, but it makes 
     * counting new messages intrinsically quadratic in the worst case.
     */
    last = 0;
    *newp = -1;
    if (*countp > 0 && !ctl->fetchall)
    {
	char id [IDLEN+1];

	if (!ctl->server.uidl) {
	    gen_send(sock, "LAST");
	    ok = pop3_ok(sock, buf);
	} else
	    ok = 1;
	if (ok == 0)
	{
	    if (sscanf(buf, "%d", &last) == 0)
	    {
		error(0, 0, "protocol error");
		return(PS_ERROR);
	    }
	    *newp = (*countp - last);
	}
 	else
 	{
	    /* grab the mailbox's UID list */
	    if ((ok = gen_transact(sock, "UIDL")) != 0)
	    {
		/* don't worry, yet! do it the slow way */
		if((ok = pop3_slowuidl( sock, ctl, countp, newp))!=0)
		{
		    error(0, 0, "protocol error while fetching UIDLs");
		    return(PS_ERROR);
		}
	    }
	    else
	    {
		int	num;

		*newp = 0;
 		while ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
		{
 		    if (buf[0] == '.')
 			break;
 		    else if (sscanf(buf, "%d %s", &num, id) == 2)
		    {
 			struct idlist	*new;

			new = save_str(&ctl->newsaved, id, UID_UNSEEN);
			new->val.status.num = num;

			/* note: ID comparison is caseblind */
			if (str_in_list(&ctl->oldsaved, id)) {
			    new->val.status.mark = UID_SEEN;
			    str_set_mark(&ctl->oldsaved, id, UID_SEEN);
			}
			else
			    (*newp)++;
		    }
 		}
 	    }
 	}
    }

    return(PS_SUCCESS);
}

static int pop3_getsizes(int sock, int count, int *sizes)
/* capture the sizes of all messages */
{
    int	ok;

    /* phase = PHASE_GETSIZES */

    if ((ok = gen_transact(sock, "LIST")) != 0)
	return(ok);
    else
    {
	char buf [POPBUFSIZE+1];

	while ((ok = gen_recv(sock, buf, sizeof(buf))) == 0)
	{
	    int num, size;

	    if (buf[0] == '.')
		break;
	    else if (sscanf(buf, "%d %d", &num, &size) == 2)
		sizes[num - 1] = size;
	}

	return(ok);
    }
}

static int pop3_is_old(int sock, struct query *ctl, int num)
/* is the given message old? */
{
    if (!ctl->oldsaved)
	return (num <= last);
    else
	/* note: ID comparison is caseblind */
        return (str_in_list(&ctl->oldsaved,
			    str_find (&ctl->newsaved, num)));
}

static int pop3_fetch(int sock, struct query *ctl, int number, int *lenp)
/* request nth message */
{
    int ok;
    char buf [POPBUFSIZE+1];

    /* phase = PHASE_FETCH */

    gen_send(sock, "RETR %d", number);
    if ((ok = pop3_ok(sock, buf)) != 0)
	return(ok);

    *lenp = -1;		/* we got sizes from the LIST response */

    return(0);
}

static int pop3_delete(int sock, struct query *ctl, int number)
/* delete a given message */
{
    /* actually, mark for deletion -- doesn't happen until QUIT time */
    return(gen_transact(sock, "DELE %d", number));
}

static int pop3_logout(int sock, struct query *ctl)
/* send logout command */
{
    int ok;

    /* phase = PHASE_LOGOUT */

    ok = gen_transact(sock, "QUIT");
    if (!ok)
	expunge_uids(ctl);

    return(ok);
}

const static struct method pop3 =
{
    "POP3",		/* Post Office Protocol v3 */
#if INET6
    "pop3",		/* standard POP3 port */
#else /* INET6 */
    110,		/* standard POP3 port */
#endif /* INET6 */
    FALSE,		/* this is not a tagged protocol */
    TRUE,		/* this uses a message delimiter */
    pop3_ok,		/* parse command response */
    NULL,		/* no password canonicalization */
    pop3_getauth,	/* get authorization */
    pop3_getrange,	/* query range of messages */
    pop3_getsizes,	/* we can get a list of sizes */
    pop3_is_old,	/* how do we tell a message is old? */
    pop3_fetch,		/* request given message */
    NULL,		/* no way to fetch body alone */
    NULL,		/* no message trailer */
    pop3_delete,	/* how to delete a message */
    pop3_logout,	/* log out, we're done */
    FALSE,		/* no, we can't re-poll */
};

int doPOP3 (struct query *ctl)
/* retrieve messages using POP3 */
{
#ifndef MBOX
    if (ctl->mailboxes->id) {
	fprintf(stderr,"Option --remote is not supported with POP3\n");
	return(PS_SYNTAX);
    }
#endif /* MBOX */
    peek_capable = FALSE;
    return(do_protocol(ctl, &pop3));
}
#endif /* POP3_ENABLE */

/* pop3.c ends here */

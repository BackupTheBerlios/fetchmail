/*
 * For license terms, see the file COPYING in this directory.
 */

/* constants designating the various supported protocols */
#define		P_AUTO		0
#define		P_POP2		2
#define		P_POP3		3
#define		P_IMAP		4
#define		P_IMAP_K4	5
#define		P_IMAP_GSS	6
#define		P_APOP		7
#define		P_RPOP		8
#define		P_ETRN		9

#if INET6
#define		KPOP_PORT	"kpop"
#else /* INET6 */
#define		KPOP_PORT	1109
#endif /* INET6 */

/* preauthentication types */
#define		A_PASSWORD	0	/* password or inline authentication */
#define		A_KERBEROS_V4	1	/* preauthenticate w/ Kerberos V4 */
#define		A_KERBEROS_V5	2	/* preauthenticate w/ Kerberos V5 */

/*
 * Definitions for buffer sizes.  We get little help on setting maxima
 * from IMAP RFCs up to 2060, so these are mostly from POP3.
 */
#define		HOSTLEN		635	/* max hostname length (RFC1123) */
#define		POPBUFSIZE	512	/* max length of response (RFC1939) */
#define		IDLEN		128	/* max length of UID (RFC1939) */

/* per RFC1939 this should be 40, but Microsoft Exchange ignores that limit */
#define		USERNAMELEN	128	/* max POP3 arg length */

/* clear a netBSD kernel parameter out of the way */ 
#undef		MSGBUFSIZE

/*
 * The RFC822 limit on message line size is just 998.  But
 * make this *way* oversized; idiot DOS-world mailers that
 * don't line-wrap properly often ship entire paragraphs as
 * lines.
 */
#define		MSGBUFSIZE	8192

#define		PASSWORDLEN	64	/* max password length */
#define		DIGESTLEN	33	/* length of MD5 digest */

/* exit code values */
#define		PS_SUCCESS	0	/* successful receipt of messages */
#define		PS_NOMAIL       1	/* no mail available */
#define		PS_SOCKET	2	/* socket I/O woes */
#define		PS_AUTHFAIL	3	/* user authorization failed */
#define		PS_PROTOCOL	4	/* protocol violation */
#define		PS_SYNTAX	5	/* command-line syntax error */
#define		PS_IOERR	6	/* bad permissions on rc file */
#define		PS_ERROR	7	/* protocol error */
#define		PS_EXCLUDE	8	/* client-side exclusion error */
#define		PS_LOCKBUSY	9	/* server responded lock busy */
#define		PS_SMTP         10      /* SMTP error */
#define		PS_DNS		11	/* fatal DNS error */	
#define		PS_UNDEFINED	12	/* something I hadn't thought of */
#define		PS_TRANSIENT	13	/* transient failure (internal use) */
#define		PS_REFUSED	14	/* mail refused (internal use) */
#define		PS_RETAINED	15	/* message retained (internal use) */
#define		PS_TRUNCATED	16	/* headers incomplete (internal use) */

/* output noise level */
#define         O_SILENT	0	/* mute, max squelch, etc. */
#define		O_NORMAL	1	/* user-friendly */
#define		O_VERBOSE	2	/* excessive */

#define		SIZETICKER	1024	/* print 1 dot per this many bytes */

/*
 * We #ifdef this and use flag rather than bool
 * to avoid a type clash with curses.h
 */
#ifndef TRUE
#define FALSE	0
#define TRUE	1
#endif /* TRUE */
typedef	char	flag;

/* we need to use zero as a flag-uninitialized value */
#define FLAG_TRUE	2
#define FLAG_FALSE	1

struct runctl
{
    char	*logfile;
    char	*idfile;
    int		poll_interval;
    flag	use_syslog;
    flag	invisible;
    char	*postmaster;
};

struct idlist
{
    char *id;
    union
    {
	struct
	{
	    short	num;
	    flag	mark;		/* UID-index information */
#define UID_UNSEEN	0		/* hasn't been seen */
#define UID_SEEN	1		/* seen, but not deleted */
#define UID_DELETED	2		/* this message has been deleted */
#define UID_EXPUNGED	3		/* this message has been expunged */ 
        }
	status;
	char *id2;
    } val;
    struct idlist *next;
};

struct hostdata		/* shared among all user connections to given server */
{
    /* rc file data */
    char *pollname;			/* poll label of host */
    char *via;				/* "true" server name if non-NULL */
    struct idlist *akalist;		/* server name first, then akas */
    struct idlist *localdomains;	/* list of pass-through domains */
    int protocol;			/* protocol type */
#if INET6
    char *service;			/* IPv6 service name */
    void *netsec;			/* IPv6 security request */
#else /* INET6 */
    int port;				/* TCP/IP service port number */
#endif /* INET6 */
    int interval;			/* # cycles to skip between polls */
    int preauthenticate;		/* preauthentication mode to try */
    int timeout;			/* inactivity timout in seconds */
    char *envelope;			/* envelope address list header */
    int envskip;			/* skip to numbered envelope header */
    char *qvirtual;			/* prefix removed from local user id */
    flag skip;				/* suppress poll in implicit mode? */
    flag dns;				/* do DNS lookup on multidrop? */
    flag uidl;				/* use RFC1725 UIDLs? */
    flag checkalias;                  /* try to resolve aliases by comparing IPs ?*/


#ifdef linux
    char *interface;
    char *monitor;
    int  monitor_io;
    struct interface_pair_s *interface_pair;
#endif /* linux */

    /* computed for internal use */
    int poll_count;			/* count of polls so far */
    char *queryname;			/* name to attempt DNS lookup on */
    char *truename;			/* "true name" of server host */
    struct hostdata *lead_server;	/* ptr to lead query for this server */
    int esmtp_options;
};

struct query
{
    /* mailserver connection controls */
    struct hostdata server;

    /* per-user data */
    struct idlist *localnames;	/* including calling user's name */
    int wildcard;		/* should unmatched names be passed through */
    char *remotename;		/* remote login name to use */
    char *password;		/* remote password to use */
    struct idlist *mailboxes;	/* list of mailboxes to check */
    struct idlist *smtphunt;	/* list of SMTP hosts to try forwarding to */
    char *smtphost;		/* actual SMTP host to point to */
    char *smtpaddress;		/* address we want to force in the delivery messages */ 
    int	antispam;		/* listener's antispam response */
    char *mda;			/* local MDA to pass mail to */
    char *preconnect;		/* pre-connection command to execute */
    char *postconnect;		/* post-connection command to execute */

    /* per-user control flags */
    flag keep;			/* if TRUE, leave messages undeleted */
    flag fetchall;		/* if TRUE, fetch all (not just unseen) */
    flag flush;			/* if TRUE, delete messages already seen */
    flag rewrite;		/* if TRUE, canonicalize recipient addresses */
    flag stripcr;		/* if TRUE, strip CRs in text */
    flag forcecr;		/* if TRUE, force CRs before LFs in text */
    flag pass8bits;		/* if TRUE, ignore Content-Transfer-Encoding */
    flag dropstatus;		/* if TRUE, drop Status lines in mail */
    flag mimedecode;		/* if TRUE, decode MIME-coded headers/coded printable*/
    int	limit;			/* limit size of retrieved messages */
    int	fetchlimit;		/* max # msgs to get in single poll */
    int	batchlimit;		/* max # msgs to pass in single SMTP session */
    int	expunge;		/* max # msgs to pass between expunges */

    struct idlist *oldsaved, *newsaved;

    /* internal use */
    flag active;		/* should we actually poll this server? */
    int errcount;		/* count transient errors in last pass */
    int smtp_socket;		/* socket descriptor for SMTP connection */
    unsigned int uid;		/* UID of user to deliver to */
    char digest [DIGESTLEN];	/* md5 digest buffer */
    struct query *next;		/* next query control block in chain */
};

/*
 * Numeric option handling.  Numeric option value of zero actually means
 * it's unspecified.  Value less than zero is zero.
 */
#define NUM_VALUE(n)		(((n) == 0) ? -1 : (n))
#define NUM_NONZERO(n)		((n) > 0)
#define NUM_ZERO(n)		((n) < 0)
#define NUM_SPECIFIED(n)	((n) != 0)

#define MULTIDROP(ctl)	(ctl->wildcard || \
				((ctl)->localnames && (ctl)->localnames->next))

struct method
{
    char *name;			/* protocol name */
#if INET6
    char *service;
#else /* INET6 */
    int	port;			/* service port */
#endif /* INET6 */
    flag tagged;		/* if true, generate & expect command tags */
    flag delimited;		/* if true, accept "." message delimiter */
    int (*parse_response)();	/* response_parsing function */
    int (*password_canonify)();	/* canonicalize password */
    int (*getauth)();		/* authorization fetcher */
    int (*getrange)();		/* get message range to fetch */
    int (*getsizes)();		/* get sizes of messages */
    int (*is_old)();		/* check for old message */
    int (*fetch_headers)();	/* fetch FROM headera given message */
    int (*fetch_body)();	/* fetch a given message */
    int (*trail)();		/* eat trailer of a message */
    int (*delete)();		/* delete method */
    int (*logout_cmd)();	/* logout command */
    flag retry;			/* can getrange poll for new messages? */
};

/*
 * Note: tags are generated with an a%04d format from a 1-origin
 * integer sequence number.  Length 4 permits transaction numbers
 * up to 9999, so we force rollover with % 10000.  There's no special
 * reason for this format other than to look like the exmples in the
 * IMAP RFCs.
 */
#define TAGLEN	6		/* 'a' + 4 digits + NUL */
extern char tag[TAGLEN];
#define TAGMOD	10000

/* list of hosts assembled from run control file and command line */
extern struct query cmd_opts, *querylist;

/* what's returned by envquery */
extern void envquery(int, char **);

/* controls the detail level of status/progress messages written to stderr */
extern int outlevel;    	/* see the O_.* constants above */
extern int yydebug;		/* enable parse debugging */

/* these get computed */
extern int batchcount;		/* count of messages sent in current batch */
extern flag peek_capable;	/* can we read msgs without setting seen? */

/* miscellaneous global controls */
extern struct runctl run;	/* global controls for this run */
extern flag nodetach;		/* if TRUE, don't detach daemon process */
extern flag quitmode;		/* if --quit was set */
extern flag check_only;		/* if --check was set */
extern char *rcfile;		/* path name of rc file */
extern int linelimit;		/* limit # lines retrieved per site */
extern flag versioninfo;	/* emit only version info */
extern char *user;		/* name of invoking user */
extern char *home;		/* home directory of invoking user */
extern char *fetchmailhost;	/* the name of the host running fetchmail */
extern int pass;		/* number of re-polling pass */
extern flag configdump;		/* dump control blocks as Python dictionary */

/* prototypes for globally callable functions */

/* error.c: Error reporting */
#if defined(HAVE_STDARG_H)
void error_init(int foreground);
void error (int status, int errnum, const char *format, ...);
void error_build (const char *format, ...);
void error_complete (int status, int errnum, const char *format, ...);
void error_at_line (int, int, const char *, unsigned int, const char *, ...);
#else
void error ();
void error_build ();
void error_complete ();
void error_at_line ();
#endif

/* driver.c: transaction support */
#if defined(HAVE_STDARG_H)
void gen_send (int sock, const char *, ... );
int gen_recv(int sock, char *buf, int size);
int gen_transact (int sock, char *, ... );
#else
void gen_send ();
int gen_recv();
int gen_transact ();
#endif

/* rfc822.c: RFC822 header parsing */
char *reply_hack(char *, const char *);
char *nxtaddr(const char *);

/* uid.c: UID support */
void initialize_saved_lists(struct query *, const char *);
struct idlist *save_str(struct idlist **, const char *, flag);
void free_str_list(struct idlist **);
void save_str_pair(struct idlist **, const char *, const char *);
void free_str_pair_list(struct idlist **);
int delete_str(struct idlist **, int);
int str_in_list(struct idlist **, const char *, const flag);
int str_nr_in_list(struct idlist **, const char *);
int str_nr_last_in_list(struct idlist **, const char *);
void str_set_mark( struct idlist **, const char *, const flag);
int count_list( struct idlist **idl );
char *str_from_nr_list( struct idlist **idl, int number );
char *str_find(struct idlist **, int);
char *idpair_find(struct idlist **, const char *);
void append_str_list(struct idlist **, struct idlist **);
void expunge_uids(struct query *);
void update_str_lists(struct query *);
void write_saved_lists(struct query *, const char *);

/* rcfile_y.y */
int prc_parse_file(const char *, const flag);
int prc_filecheck(const char *, const flag);

/* base64.c */
void to64frombits(unsigned char *, const unsigned char *, int);
int from64tobits(char *, const char *);

/* unmime.c */
/* Bit-mask returned by MimeBodyType */
#define MSG_IS_7BIT       0x01
#define MSG_IS_8BIT       0x02
#define MSG_NEEDS_DECODE  0x80
extern void UnMimeHeader(unsigned char *buf);
extern int  MimeBodyType(unsigned char *hdrs, int WantDecode);
extern int  UnMimeBodyline(unsigned char **buf, int collapsedoubledot);

/* interface.c */
void interface_parse(char *, struct hostdata *);
void interface_note_activity(struct hostdata *);
int interface_approve(struct hostdata *);

/* xmalloc.c */
#if defined(HAVE_VOIDPOINTER)
#define XMALLOCTYPE void
#else
#define XMALLOCTYPE char
#endif
XMALLOCTYPE *xmalloc(int);
XMALLOCTYPE *xrealloc(XMALLOCTYPE *, int);
char *xstrdup(const char *);

/* protocol driver and methods */
int do_protocol(struct query *, const struct method *);
int doPOP2 (struct query *); 
int doPOP3 (struct query *);
int doIMAP (struct query *);
int doETRN (struct query *);

/* miscellanea */
struct query *hostalloc(struct query *); 
int parsecmdline (int, char **, struct runctl *, struct query *);
char *MD5Digest (unsigned char *);
int POP3_auth_rpa(unsigned char *, unsigned char *, int socket);
int daemonize(const char *, void (*)(int));
char *getpassword(char *);
void escapes(const char *, char *);
char *visbuf(const char *);
char *showproto(int);
void dump_config(struct runctl *runp, struct query *querylist);
int is_host_alias(const char *, struct query *);

void yyerror(const char *);
int yylex(void);

#ifdef __EMX__
void itimerthread(void*);
/* Have to include these first to avoid errors from redefining getcwd
   and chdir.  They're re-include protected in EMX, so it's okay, I
   guess.  */
#include <stdlib.h>
#include <unistd.h>
/* Redefine getcwd and chdir to get drive-letter support so we can
   find all of our lock files and stuff. */
#define getcwd _getcwd2
#define chdir _chdir2
#endif

#define STRING_DISABLED	(char *)-1

/* fetchmail.h ends here */

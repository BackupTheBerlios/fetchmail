/* Copyright 1993-95 by Carl Harris, Jr. Copyright 1996 by Eric S. Raymond
 * All rights reserved.
 * For license terms, see the file COPYING in this directory.
 */

/***********************************************************************
  module:       fetchmail.c
  project:      fetchmail
  programmer:   Carl Harris, ceharris@mal.com
		Extensively hacked and improved by esr.
  description:	main driver module for fetchmail

 ***********************************************************************/


#include <config.h>

#include <stdio.h>

#if defined(STDC_HEADERS)
#include <stdlib.h>
#include <string.h>
#endif

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#include <signal.h>
#include <pwd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "fetchmail.h"

#ifdef HAVE_PROTOTYPES
/* prototypes for internal functions */
static int showversioninfo (void);
static int dump_options (struct hostrec *queryctl);
static int query_host(struct hostrec *queryctl);
#endif

/* controls the detail level of status/progress messages written to stderr */
int outlevel;    	/* see the O_.* constants above */
int yydebug;		/* enable parse debugging */

/* daemon mode control */
int poll_interval;	/* poll interval in seconds */
char *logfile;		/* log file for daemon mode */
int quitmode;		/* if --quit was set */

/* miscellaneous global controls */
char *rcfile;		/* path name of rc file */
int versioninfo;	/* emit only version info */

/*********************************************************************
  function:      main
  description:   main driver routine 
  arguments:     
    argc         argument count as passed by runtime startup code.
    argv         argument strings as passed by runtime startup code.

  return value:  an exit status code for the shell -- see the 
                 PS_.* constants defined above.
  calls:         parsecmdline, setdefaults, openuserfolder, doPOP2.
  globals:       none.
 *********************************************************************/

static void termhook();
static char *lockfile;
static int popstatus;
static struct hostrec *hostp;

main (argc,argv)
int argc;
char **argv;
{ 
    int mboxfd, st;
    struct hostrec def_opts;
    int parsestatus, implicitmode;
    char *servername, *user, *home, *tmpdir, tmpbuf[BUFSIZ]; 
    FILE	*tmpfp;
    pid_t pid;

    memset(&def_opts, '\0', sizeof(struct hostrec));

    if ((user = getenv("USER")) == (char *)NULL)
        user = getenv("LOGNAME");

    if ((user == (char *)NULL) || (home = getenv("HOME")) == (char *)NULL)
    {
	struct passwd *pw;

	if ((pw = getpwuid(getuid())) != NULL)
	{
	    user = pw->pw_name;
	    home = pw->pw_dir;
	}
	else
	{
	    fprintf(stderr,"I can't find your name and home directory!\n");
	    exit(PS_UNDEFINED);
	}
    }

    def_opts.protocol = P_AUTO;

    strcpy(def_opts.remotename, user);
    strcpy(def_opts.smtphost, "localhost");

    rcfile = 
	(char *) xmalloc(strlen(home)+strlen(RCFILE_NAME)+2);
    strcpy(rcfile, home);
    strcat(rcfile, "/");
    strcat(rcfile, RCFILE_NAME);

    outlevel = O_NORMAL;

    if ((parsestatus = parsecmdline(argc,argv,&cmd_opts)) < 0)
	exit(PS_SYNTAX);

    if (versioninfo)
	showversioninfo();

    /* this builds the host list */
    if (prc_parse_file(rcfile) != 0)
	exit(PS_SYNTAX);

    if (implicitmode = (optind >= argc))
    {
	for (hostp = hostlist; hostp; hostp = hostp->next)
	    hostp->active = TRUE;
    }
    else
	for (; optind < argc; optind++) 
	{
	    /*
	     * If hostname corresponds to a host known from the rc file,
	     * simply declare it active.  Otherwise synthesize a host
	     * record from command line and defaults
	     */
	    for (hostp = hostlist; hostp; hostp = hostp->next)
		if (strcmp(hostp->servername, argv[optind]) == 0)
		    goto foundit;

	    hostp = hostalloc(&cmd_opts);
	    strcpy(hostp->servername, argv[optind]);

	foundit:
	    hostp->active = TRUE;
	}

    /* merge in defaults for empty fields, then lose defaults record */ 
    if (strcmp(hostlist->servername, "defaults") == 0)
    {
	optmerge(hostlist, &def_opts);
	for (hostp = hostlist; hostp; hostp = hostp->next)
	    optmerge(hostp, hostlist);
	hostlist = hostlist->next;
    }

    /* do sanity checks and prepare internal fields */
    for (hostp = hostlist; hostp; hostp = hostp->next)
    {
	/* if rc file didn't supply a localname, default appropriately */
	if (!hostp->localname[0])
	    strcpy(hostp->localname, hostp->remotename);

	/* sanity checks */
	if (hostp->port < 0)
	{
	    (void) fprintf(stderr,
			   "%s configuration invalid, port number cannot be negative",
			   hostp->servername);
	    exit(PS_SYNTAX);
	}
	if (hostp->protocol == P_RPOP && hostp->port >= 1024) 
	{
	    (void) fprintf(stderr,
			   "%s configuration invalid, can't do RPOP to an unprivileged port\n",
			   hostp->servername);
	    exit(PS_SYNTAX);
	}

	/* expand MDA commands */
	if (hostp->mda[0])
	{
	    int argi;
	    char *argp;

	    /* expand the %s escape if any before parsing */
	    sprintf(hostp->mdabuf, hostp->mda, hostp->localname);

	    /* now punch nulls into the delimiting whitespace in the args */
	    for (argp = hostp->mdabuf, argi = 1; *argp != '\0'; argi++)
	    {
		hostp->mda_argv[argi] = argp;
		while (!(*argp == '\0' || isspace(*argp)))
		    argp++;
		if (*argp != '\0')
		    *(argp++) = '\0';  
	    }

	    hostp->mda_argv[argi] = (char *)NULL;

	    hostp->mda_argv[0] = hostp->mda_argv[1];
	    if ((argp = strrchr(hostp->mda_argv[1], '/')) != (char *)NULL)
		hostp->mda_argv[1] = argp + 1 ;
	}
    }

    /* set up to do lock protocol */
    if ((tmpdir = getenv("TMPDIR")) == (char *)NULL)
	tmpdir = "/tmp";
    strcpy(tmpbuf, tmpdir);
    strcat(tmpbuf, "/fetchmail-");
    gethostname(tmpbuf + strlen(tmpbuf), HOSTLEN+1);
    strcat(tmpbuf, "-");
    strcat(tmpbuf, user);

    /* perhaps we just want to check options? */
    if (versioninfo) {
	    printf("Taking options from command line");
	    if (access(rcfile, 0))
		printf("\n");
	    else
		printf(" and %s\n", rcfile);
	    if (outlevel == O_VERBOSE)
		printf("Lockfile at %s\n", tmpbuf);
	for (hostp = hostlist; hostp; hostp = hostp->next) {
	    if (hostp->active && !(implicitmode && hostp->skip))
		dump_params(hostp);
	}
	if (hostlist == NULL)
	    (void) fprintf(stderr,
		"No mailservers set up -- perhaps %s is missing?\n",
			  rcfile);
	exit(0);
    }
    else if (hostlist == NULL) {
	(void) fputs("fetchmail: no mailservers have been specified.\n", stderr);
	exit(PS_SYNTAX);
    }

    if ((lockfile = (char *) malloc(strlen(tmpbuf) + 1)) == NULL)
    {
	fprintf(stderr,"fetchmail: cannot allocate memory for lock name.\n");
	exit(PS_EXCLUDE);
    }
    else
	(void) strcpy(lockfile, tmpbuf);

    /* perhaps user asked us to remove a lock */
    if (quitmode)
    {
	FILE* fp;

	if ( (fp = fopen(lockfile, "r")) == NULL ) {
	    fprintf(stderr,"fetchmail: no other fetchmail is running\n");
	    return(PS_EXCLUDE);
	}
  
	fscanf(fp,"%d",&pid);
	fprintf(stderr,"fetchmail: killing fetchmail at PID %d\n",pid);
	if ( kill(pid,SIGTERM) < 0 )
	    fprintf(stderr,"fetchmail: error killing the process %d.\n",pid);
	else
	    fprintf(stderr,"fetchmail: fetchmail at %d is dead.\n", pid);
  
	fclose(fp);
	remove(lockfile);
	exit(0);
    }


    /* beyond here we don't want more than one fetchmail running per user */
    umask(0077);
    if ( (tmpfp = fopen(lockfile, "r")) != NULL ) {
	fscanf(tmpfp,"%d",&pid);
	fprintf(stderr,"Another session appears to be running at pid %d.\nIf you are sure that this is incorrect, remove %s file.\n",pid,lockfile);
	fclose(tmpfp);
	return(PS_EXCLUDE);
    }

    /* pick up interactively any passwords we need but don't have */ 
    for (hostp = hostlist; hostp; hostp = hostp->next)
	if (hostp->active && !(implicitmode && hostp->skip) && !hostp->password[0])
	{
	    (void) sprintf(tmpbuf, "Enter password for %s@%s: ",
			   hostp->remotename, hostp->servername);
	    (void) strncpy(hostp->password,
			   (char *)getpassword(tmpbuf),PASSWORDLEN-1);
	}

    /*
     * Maybe time to go to demon mode...
     */
    if (poll_interval)
	daemonize(logfile, termhook);

    /* if not locked, assert a lock */
    signal(SIGABRT, termhook);
    signal(SIGINT, termhook);
    signal(SIGTERM, termhook);
    signal(SIGALRM, termhook);
    signal(SIGHUP, termhook);
    signal(SIGPIPE, termhook);
    signal(SIGQUIT, termhook);
    if ( (tmpfp = fopen(lockfile,"w")) != NULL ) {
	fprintf(tmpfp,"%d",getpid());
	fclose(tmpfp);
    }

    /*
     * Query all hosts. If there's only one, the error return will
     * reflect the status of that transaction.
     */
    do {
	for (hostp = hostlist; hostp; hostp = hostp->next) {
	    if (hostp->active && !(implicitmode && hostp->skip))
		popstatus = query_host(hostp);
	}

	sleep(poll_interval);
    } while
	(poll_interval);

    if (outlevel == O_VERBOSE)
	fprintf(stderr, "normal termination, status %d\n", popstatus);

    termhook(0);
    exit(popstatus);
}

void termhook(int sig)
/* to be executed on normal or signal-induced termination */
{
    if (sig != 0)
	fprintf(stderr, "terminated with signal %d\n", sig);

    unlink(lockfile);
    exit(popstatus);
}

/*********************************************************************
  function:      showproto
  description:   protocol index to name mapping
  arguments:
    proto        protocol index
  return value:  string name of protocol
  calls:         none.
  globals:       none.
 *********************************************************************/

static char *showproto(proto)
int proto;
{
    switch (proto)
    {
    case P_AUTO: return("auto"); break;
    case P_POP2: return("POP2"); break;
    case P_POP3: return("POP3"); break;
    case P_IMAP: return("IMAP"); break;
    case P_APOP: return("APOP"); break;
    case P_RPOP: return("RPOP"); break;
    default: return("unknown?!?"); break;
    }
}

/*
 * Sequence of protocols to try when autoprobing, most capable to least.
 */
static const int autoprobe[] = {P_IMAP, P_POP3, P_POP2};

static int query_host(queryctl)
/* perform fetch transaction with single host */
struct hostrec *queryctl;
{
    int i, st;

    if (outlevel == O_VERBOSE)
    {
	time_t now;

	time(&now);
	fprintf(stderr, "Querying %s (protocol %s) at %s",
	    queryctl->servername, showproto(queryctl->protocol), ctime(&now));
    }
    switch (queryctl->protocol) {
    case P_AUTO:
	for (i = 0; i < sizeof(autoprobe)/sizeof(autoprobe[0]); i++)
	{
	    queryctl->protocol = autoprobe[i];
	    if ((st = query_host(queryctl)) == PS_SUCCESS || st == PS_NOMAIL || st == PS_AUTHFAIL)
		break;
	}
	queryctl->protocol = P_AUTO;
	return(st);
	break;
    case P_POP2:
	return(doPOP2(queryctl));
	break;
    case P_POP3:
    case P_APOP:
    case P_RPOP:
	return(doPOP3(queryctl));
	break;
    case P_IMAP:
	return(doIMAP(queryctl));
	break;
    default:
	fprintf(stderr,"fetchmail: unsupported protocol selected.\n");
	return(PS_PROTOCOL);
    }
}
 
/*********************************************************************
  function:      showversioninfo
  description:   display program release
  arguments:     none.
  return value:  none.
  calls:         none.
  globals:       none.
 *********************************************************************/

static int showversioninfo()
{
    printf("This is fetchmail release %s\n",RELEASE_ID);
}

/*********************************************************************
  function:      dump_params
  description:   display program options in English
  arguments:
    queryctl      merged options

  return value:  none.
  calls:         none.
  globals:       outlimit.
*********************************************************************/

int dump_params (queryctl)
struct hostrec *queryctl;
{
    printf("Options for %s retrieving from %s:\n",
	   hostp->localname, hostp->servername);
    if (queryctl->skip || outlevel == O_VERBOSE)
	printf("  This host will%s be queried when no host is specified.\n",
	       queryctl->skip ? " not" : "");
    printf("  Username = '%s'\n", queryctl->remotename);
    if (queryctl->password[0] == '\0')
	printf("  Password will be prompted for.\n");
    else if (outlevel == O_VERBOSE)
	if (queryctl->protocol == P_APOP)
	    printf("  APOP secret = '%s'\n", queryctl->password);
	else if (queryctl->protocol == P_RPOP)
	    printf("  RPOP secret = '%s'\n", queryctl->password);
        else
	    printf("  Password = '%s'\n", queryctl->password);
    printf("  Protocol is %s", showproto(queryctl->protocol));
    if (queryctl->port)
	printf(" (using port %d)", queryctl->port);
    else if (outlevel == O_VERBOSE)
	printf(" (using default port)");
    putchar('\n');

    printf("  Fetched messages will%s be kept on the server (--keep %s).\n",
	   queryctl->keep ? "" : " not",
	   queryctl->keep ? "on" : "off");
    printf("  %s messages will be retrieved (--all %s).\n",
	   queryctl->fetchall ? "All" : "Only new",
	   queryctl->fetchall ? "on" : "off");
    printf("  Old messages will%s be flushed before message retrieval (--flush %s).\n",
	   queryctl->flush ? "" : " not",
	   queryctl->flush ? "on" : "off");
    printf("  Rewrite of server-local addresses is %sabled (--norewrite %s)\n",
	   queryctl->norewrite ? "dis" : "en",
	   queryctl->norewrite ? "on" : "off");
    if (queryctl->mda[0])
    {
	char **cp;

	printf("  Messages will be delivered with %s, args:",
	       queryctl->mda_argv[0]);
	for (cp = queryctl->mda_argv+1; *cp; cp++)
	    printf(" %s", *cp);
	putchar('\n');
    }
    else
	printf("  Messages will be SMTP-forwarded to '%s'\n", queryctl->smtphost);
}

/*********************************************************************
  function:      openmailpipe
  description:   open a one-way pipe to the mail delivery agent.
  arguments:     
    queryctl     fully-determined options (i.e. parsed, defaults invoked,
                 etc).

  return value:  open file descriptor for the pipe or -1.
  calls:         none.
 *********************************************************************/

int openmailpipe (queryctl)
struct hostrec *queryctl;
{
    int pipefd [2];
    int childpid;

    if (pipe(pipefd) < 0) {
	perror("fetchmail: openmailpipe: pipe");
	return(-1);
    }
    if ((childpid = fork()) < 0) {
	perror("fetchmail: openmailpipe: fork");
	return(-1);
    }
    else if (childpid == 0) {

	/* in child process space */
	close(pipefd[1]);  /* close the 'write' end of the pipe */
	close(0);          /* get rid of inherited stdin */
	if (dup(pipefd[0]) != 0) {
	    fputs("fetchmail: openmailpipe: dup() failed\n",stderr);
	    exit(1);
	}

	execv(queryctl->mda_argv[0], queryctl->mda_argv + 1);

	/* if we got here, an error occurred */
	perror("fetchmail: openmailpipe: exec");
	_exit(PS_SYNTAX);

    }

    /* in the parent process space */
    close(pipefd[0]);  /* close the 'read' end of the pipe */
    return(pipefd[1]);
}

/*********************************************************************
  function:      closemailpipe
  description:   close pipe to the mail delivery agent.
  arguments:     
    queryctl     fully-determined options record
    fd           pipe descriptor.

  return value:  0 if success, else -1.
  calls:         none.
  globals:       none.
 *********************************************************************/

int closemailpipe (fd)
int fd;
{
    int err;
    int childpid;

    if (outlevel == O_VERBOSE)
	fprintf(stderr, "about to close pipe %d\n", fd);

    err = close(fd);
#if defined(STDC_HEADERS)
    childpid = wait(NULL);
#else
    childpid = wait((int *) 0);
#endif
    if (err)
	perror("fetchmail: closemailpipe: close");

    if (outlevel == O_VERBOSE)
	fprintf(stderr, "closed pipe %d\n", fd);
  
    return(err);
}

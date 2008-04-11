/*

Copyright (c) 2004 Jeremy Kerr & Rusty Russell

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <log.h>
#include <kernelenv.h>
#include <utils.h>
#include <tui.h>
#include <sys/types.h>
#include <sys/wait.h>

static bool failtest = false;
unsigned int suppress_failtest;
static unsigned int fails = 0, excessive_fails = 2;
unsigned int failpoints = 0;
static const char *failtest_no_report = NULL;

struct fail_decision
{
	struct list_head list;
	char *location;
	unsigned int tui_line;
	bool failed;
};

static LIST_HEAD(decisions);

/* Failure path to follow (initialized by --failpath). */
static const char *failpath = NULL, *orig_failpath;

/*** XML Argument:
    <section id="a:failtest">
     <title><option>--failtest</option></title>
     <subtitle>Try every combination of external failures</subtitle>

     <para>The <option>--failtest</option> option runs the script,
     but deliberately inserts failures (currently allocation failures
     and user-copying failures).  At each potential failure, both
     failure and success will be simulated: where we fail, the script
     will no doubt fail later which is OK, but we ensure that the
     entire system doesn't fall over or leak memory.  If an error is
     found, the path of successes/failures will be printed out,
     which can be replayed using <option>--failpath</option></para>
    </section>
*/
static void cmdline_failtest(struct option *opt)
{
	failtest = true;
}
cmdline_opt("failtest", 0, 0, cmdline_failtest);


/*** XML Argument:
    <section id="a:failpath">
     <title><option>--failpath
      <replaceable>path</replaceable></option></title>
     <subtitle>Replay a failure path</subtitle>
     <para>Given a failure path, (from <option>--failtest</option>), this will
      replay the sequence of sucesses/failures, allowing debugging.  The input
      should be the same as the original which caused the failure.
      </para>

     <para>This testing can be slow, but allows for testing of failure
      paths which would otherwise be very difficult to test
     automatically.</para>
    </section>
*/
static void cmdline_failpath(struct option *opt)
{
	extern char *optarg;
	if (!optarg)
		barf("failtest option requires an argument");
	orig_failpath = failpath = optarg;
}
cmdline_opt("failpath", 1, 0, cmdline_failpath);

/*** XML Argument:
    <section id="a:failtest-no-report">
     <title><option>--failtest-no-report
      <replaceable>function</replaceable></option></title>
     <subtitle>Exclude a function from excessive failure reports</subtitle>

     <para>Sometimes code deliberately ignores the failures of a
     certain function.  This suppresses complaints about that for any
     functions containing this name.</para> </section>
*/
static void cmdline_failtest_no_report(struct option *opt)
{
	extern char *optarg;
	if (!optarg)
		barf("failtest-no-report option requires an argument");
	failtest_no_report = optarg;
}
cmdline_opt("failtest-no-report", 1, 0, cmdline_failtest_no_report);

bool get_failtest(void)
{
	return failtest;
}

/* Separate function to make .gdbinit easier */
static bool failpath_fail(void)
{
	return true;
}

static bool do_failpath(const char *func)
{
	if (*failpath == '[') {
		failpath++;
		if (strncmp(failpath, func, strlen(func)) != 0
		    || failpath[strlen(func)] != ']')
			barf("Failpath expected %.*s not %s\n",
			     strcspn(failpath, "]"), failpath, func);
		failpath += strlen(func) + 1;
	}

	if (*failpath == ':') {
		unsigned long line;
		char *after;
		failpath++;
		line = strtoul(failpath, &after, 10);
		if (*after != ':')
			barf("Bad failure path line number %s\n",
			     failpath);
		if (line != tui_linenum)
			barf("Unexpected line number %lu vs %u\n",
			     line, tui_linenum);
		failpath = after+1;
	}

	switch ((failpath++)[0]) {
	case 'F':
	case 'f':
		return failpath_fail();
	case 'S':
	case 's':
		return false;
	case 0:
		failpath = NULL;
		return false;
	default:
		barf("Failpath '%c' failed to path",
		     failpath[-1]);
	}
}

static char *failpath_string_for_line(struct fail_decision *dec)
{
	char *ret = NULL;
	struct fail_decision *i, *prev;
	unsigned int linenum = dec->tui_line;

	/* Find first one on this line. */
	list_for_each_entry_reverse(prev, &dec->list, list)
		if (&prev->list == &decisions || prev->tui_line != linenum)
			break;

	list_for_each_entry(i, &prev->list, list) {
		if (i->tui_line != linenum)
			break;
		ret = talloc_asprintf_append(ret, "[%s]%c",
					     i->location,
					     i->failed ? 'F' : 'S');
	}
	return ret;
}

static char *failpath_string(void)
{
	char *ret = NULL;
	struct fail_decision *i;

	list_for_each_entry(i, &decisions, list)
		ret = talloc_asprintf_append(ret, "[%s]:%i:%c",
					     i->location, i->tui_line,
					     i->failed ? 'F' : 'S');
	return ret;
}

static void warn_failure(void)
{
	char *warning;
	struct fail_decision *i;
	int last_warning = 0;

	nfsim_log(LOG_ALWAYS, "WARNING: test %s ignores failures at %s",
		  nfsim_testname ?: "", failpath_string());

	list_for_each_entry(i, &decisions, list) {
		if (i->failed && i->tui_line > last_warning) {
			warning = failpath_string_for_line(i);
			nfsim_log(LOG_ALWAYS, " Line %i: %s",
				  i->tui_line, warning);
			talloc_free(warning);
			last_warning = i->tui_line;
		}
	}
}

/* Should I fail at this point?  Once only: it would be too expensive
 * to fail at every possible call. */
bool should_i_fail_once(const char *location)
{
	char *p;
	struct fail_decision *i;

	if (suppress_failtest)
		return false;

	if (failpath) {
		p = strstr(orig_failpath ?: "", location);
		if (p && p <= failpath
		    && p[-1] == '[' && p[strlen(location)] == ']')
			return false;

		return do_failpath(location);
	}

	list_for_each_entry(i, &decisions, list)
		if (streq(location, i->location))
			return false;

	if (should_i_fail(location)) {
		excessive_fails++;
		return true;
	}
	return false;
}

/* Should I fail at this point? */
bool should_i_fail(const char *func)
{
	pid_t child;
	int status;
	struct fail_decision *dec;

	if (suppress_failtest)
		return false;

	if (failpath)
		return do_failpath(func);

	failpoints++;
	if (!failtest)
		return false;

	/* If a testcase ignores a spuriously-inserted failure, it's
	 * not specific enough, and we risk doing 2^n tests! */
	if (fails > excessive_fails) {
		static bool warned = false;
		if (!warned++)
			warn_failure();
	}

	dec = talloc(NULL, struct fail_decision);
	dec->location = talloc_strdup(dec, func);
	dec->tui_line = tui_linenum;
	list_add_tail(&dec->list, &decisions);

	fflush(stdout);
	child = fork();
	if (child == -1)
		barf_perror("fork failed for failtest!");

	/* The child actually fails.  The script will screw up at this
	 * point, but should not crash. */
	if (child == 0) {
		dec->failed = true;
		if (!failtest_no_report || !strstr(func, failtest_no_report))
			fails++;
		/* If we're talking to iptables, it has to fork too. */
		fork_other_program();
		return true;
	}

	dec->failed = false;
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			barf_perror("failtest waitpid failed for child %i",
				    (int)child);
	}

	/* If child succeeded, or mere script failure, continue. */
	if (WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_SUCCESS
				  || WEXITSTATUS(status) == EXIT_SCRIPTFAIL))
		return false;

	/* Report unless child already reported it. */
	if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SILENT) {
		/* Reproduce child's path */
		dec->failed = true;
		nfsim_log(LOG_ALWAYS, "Child %s %i on failure path: %s",
			  WIFEXITED(status) ? "exited" : "signalled",
			  WIFEXITED(status) ? WEXITSTATUS(status)
			  : WTERMSIG(status), failpath_string());
	}
	exit(EXIT_SILENT);
}

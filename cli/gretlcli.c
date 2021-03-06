/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

/* command-line client program for libgretl */

#include "libgretl.h"
#include "version.h"
#include "monte_carlo.h"
#include "var.h"
#include "system.h"
#include "gretl_restrict.h"
#include "gretl_func.h"
#include "gretl_help.h"
#include "libset.h"
#include "cmd_private.h"
#include "flow_control.h"
#include "objstack.h"
#include "gretl_xml.h"
#include "gretl_string_table.h"
#include "dbread.h"
#include "uservar.h"
#include "csvdata.h"
#ifdef USE_CURL
# include "gretl_www.h"
#endif

#include <dirent.h>

#ifdef WIN32
# include "gretl_win32.h"
#else
# include <sys/stat.h>
# include <sys/types.h>
# include <fcntl.h>
# include <unistd.h>
#endif 

#ifdef HAVE_READLINE
# include <readline/readline.h>
/* readline functions from complete.c */
extern char *rl_gets (char **line_read, const char *prompt);
extern void initialize_readline (void);
#endif /* HAVE_READLINE */

char datafile[MAXLEN];
char cmdfile[MAXLEN];
FILE *fb;
int batch;
int runit;
int batch_stdin;
int data_status;
char linebak[MAXLINE];      /* for storing comments */
char *line_read;

static int cli_exec_line (ExecState *s, DATASET *dset, PRN *cmdprn);
static int push_input_file (FILE *fp);
static FILE *pop_input_file (void);
static int cli_saved_object_action (const char *line, 
				    DATASET *dset, 
				    PRN *prn);

static int parse_options (int *pargc, char ***pargv, gretlopt *popt, 
			  double *scriptval, char *fname)
{
    char **argv;
    int argc, gotfile = 0;
    gretlopt opt = OPT_NONE;
    int err = 0;

    *fname = '\0';

    if (pargv == NULL) {
	return 0;
    }

    argc = *pargc;
    argv = *pargv;

    while (*++argv) {
	const char *s = *argv;

	if (!strcmp(s, "-e") || !strncmp(s, "--english", 9)) { 
	    opt |= OPT_ENGLISH;
	} else if (!strcmp(s, "-b") || !strncmp(s, "--batch", 7)) {
	    opt |= OPT_BATCH;
	} else if (!strcmp(s, "-h") || !strcmp(s, "--help")) { 
	    opt |= OPT_HELP;
	} else if (!strcmp(s, "-v") || !strcmp(s, "--version")) { 
	    opt |= OPT_VERSION;
	} else if (!strcmp(s, "-r") || !strncmp(s, "--run", 5)) { 
	    opt |= OPT_RUNIT;
	} else if (!strcmp(s, "-d") || !strncmp(s, "--db", 4)) { 
	    opt |= OPT_DBOPEN;
	} else if (!strcmp(s, "-w") || !strncmp(s, "--webdb", 7)) { 
	    opt |= OPT_WEBDB;
	} else if (!strcmp(s, "-c") || !strncmp(s, "--dump", 6)) {
	    opt |= OPT_DUMP;
	} else if (!strcmp(s, "-q") || !strcmp(s, "--quiet")) { 
	    opt |= OPT_QUIET;
	} else if (!strcmp(s, "-m") || !strcmp(s, "--makepkg")) { 
	    opt |= OPT_MAKEPKG;
	} else if (!strcmp(s, "-i") || !strcmp(s, "--instpkg")) { 
	    opt |= OPT_INSTPKG;
	} else if (!strcmp(s, "-t") || !strcmp(s, "--tool")) {
	    opt |= (OPT_TOOL | OPT_BATCH);
	} else if (!strncmp(s, "--scriptopt=", 12)) {
	    *scriptval = atof(s + 12);
	} else if (*s == '-' && *(s+1) != '\0') {
	    /* spurious option? */
	    fprintf(stderr, "Bad option: %s\n", s);
	    err = E_DATA;
	    break;
	} else if (!gotfile) {
	    strncat(fname, s, MAXLEN - 1);
	    gotfile = 1;
	}

	argc--;
    }

    if (!err) {
	err = incompatible_options(opt, OPT_BATCH | OPT_RUNIT | 
				   OPT_DBOPEN | OPT_WEBDB | OPT_MAKEPKG);
	if (!err) {
	    err = incompatible_options(opt, OPT_ENGLISH | OPT_BASQUE);
	}
	if (!err) {
	    err = incompatible_options(opt, OPT_MAKEPKG | OPT_INSTPKG);
	}	
    }
    
    *pargc = argc;
    *pargv = argv;
    *popt = opt;

    return err;
}

static void usage (int err)
{
    logo(0);

    printf(_("\nYou may supply the name of a data file on the command line.\n"
	     "Options:\n"
	     " -b or --batch     Process a command script and exit.\n"
	     " -r or --run       Run a script then hand control to command line.\n"
	     " -m or --makepkg   Run a script and create a package from it.\n"
	     " -i or --instpkg   Install a specified function package.\n"
	     " -h or --help      Print this info and exit.\n"
	     " -v or --version   Print version info and exit.\n"
	     " -e or --english   Force use of English rather than translation.\n"
	     " -q or --quiet     Print less verbose program information.\n"
	     " -t or --tool      Operate silently.\n"
	     "Example of batch mode usage:\n"
	     " gretlcli -b myfile.inp > myfile.out\n"
	     "Example of run mode usage:\n"
	     " gretlcli -r myfile.inp\n"));

    printf(_("\nSpecial batch-mode option:\n"
	     " --scriptopt=<value> sets a scalar value, accessible to a script\n"
	     " under the name \"scriptopt\"\n\n"));

    if (err) {
	exit(EXIT_FAILURE);
    } else {
	exit(EXIT_SUCCESS);
    }
}

#if defined(OPENMP_BUILD) && !defined(WIN32) && !defined(OS_OSX)

static void check_blas_threading (int tool, int quiet)
{
    char *s1, *s2;

    if (get_openblas_details(&s1, &s2) && !strcmp(s2, "pthreads")) {
	if (tool || quiet) {
	    fprintf(stderr, "Disabling OpenBLAS multi-threading "
		    "(OpenMP/pthreads collision)\n");
	} else {
	    puts("\n*** Warning ***\n*\n"
		 "* gretl is built using OpenMP, but is linked against\n"
		 "* OpenBLAS parallelized via pthreads. This combination\n"
		 "* of threading mechanisms is not recommended. Ideally,\n"
		 "* OpenBLAS should also use OpenMP.");
	}
	/* do we really need this? */
	gretl_setenv("OPENBLAS_NUM_THREADS", "1");
    }
}

#endif

static void gretl_abort (char *line)
{
    const char *tokline = get_parser_errline();
    
    fprintf(stderr, _("\ngretlcli: error executing script: halting\n"));

    if (tokline != NULL && *tokline != '\0' && strcmp(tokline, line)) {
	fprintf(stderr, "> %s\n", tokline);
    }
    
    if (*line != '\0') {
	fprintf(stderr, "> %s\n", line);
    }
    
    exit(EXIT_FAILURE);
}

static void noalloc (void)
{
    fputs(_("Out of memory!\n"), stderr);
    exit(EXIT_FAILURE);
}

static int file_get_line (ExecState *s)
{
    char *line = s->line;
    int len = 0;

    memset(line, 0, MAXLINE);
    
    if (fgets(line, MAXLINE, fb) == NULL) {
	/* no more input from current source */
	gretl_exec_state_uncomment(s);
    } else {
	len = strlen(line);
    }

    if (*line == '\0') {
	strcpy(line, "quit");
	s->cmd->ci = QUIT;
    } else if (len == MAXLINE - 1 && line[len-1] != '\n') {
	return E_TOOLONG;
    } else {
	*linebak = '\0';
	strncat(linebak, line, MAXLINE-1);
	tailstrip(linebak);
    }

    if (gretl_echo_on() && s->cmd->ci == RUN && batch && *line == '(') {
	printf("%s", line);
	*linebak = '\0';
    }

    return 0;
}

#ifdef ENABLE_NLS

static void nls_init (void)
{
# ifdef WIN32
    char LOCALEDIR[MAXLEN];

    build_path(LOCALEDIR, gretl_home(), "locale", NULL);
# endif /* WIN32 */
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE); 
    iso_gettext("@CLI_INIT");

    gretl_setenv("LC_NUMERIC", "");
    setlocale(LC_NUMERIC, "");
    reset_local_decpoint();
# ifdef WIN32
    cli_set_win32_charset(PACKAGE);
# endif
}

#endif /* ENABLE_NLS */

static int cli_clear_data (CMD *cmd, DATASET *dset, MODEL *model)
{
    gretlopt clearopt = 0;
    int err = 0;

    if (cmd->ci == CLEAR) {
	clearopt = cmd->opt;
    } else if (cmd->opt & OPT_P) {
	/* --preserve: clear dataset only */
	clearopt = OPT_D;
    } else if (csv_open_needs_matrix(cmd->opt)) {
	clearopt = OPT_D;
    }

    *datafile = '\0';

    if (dset->Z != NULL) {
	err = restore_full_sample(dset, NULL); 
	free_Z(dset); 
    }

    clear_datainfo(dset, CLEAR_FULL);
    data_status = 0;

    clear_model(model);

    if (clearopt & OPT_D) {
	libgretl_session_cleanup(SESSION_CLEAR_DATASET);
    } else {
	libgretl_session_cleanup(SESSION_CLEAR_ALL);
    }

    set_model_count(0);
    gretl_cmd_destroy_context(cmd);

    return err;
}

static const char *get_prompt (ExecState *s)
{
    if (s->flags & DEBUG_EXEC) {
	return "$ ";
    } else if (gretl_compiling_function() ||
	       gretl_compiling_loop()) {
	return "> ";
    } else {
	return "? ";
    }
}

/* this function is set up as it is to make it available for debugging
   purposes */

static int get_interactive_line (void *p)
{
    ExecState *s = (ExecState *) p;
    const char *prompt = get_prompt(s);
    int err = 0;

#ifdef HAVE_READLINE
    rl_gets(&line_read, prompt);

    if (line_read == NULL) {
	strcpy(s->line, "quit");
    } else if (strlen(line_read) > MAXLINE - 2) {
	err = E_TOOLONG;
    } else {
	*s->line = '\0';
	strncat(s->line, line_read, MAXLINE - 2);
	strcat(s->line, "\n");
    }
#else
    printf("%s", prompt);
    fflush(stdout);
    file_get_line(s); /* note: "file" = stdin here */
#endif

    return err;
}

static int cli_get_input_line (ExecState *s)
{
    int err = 0;

    if (runit || batch) {
	/* reading from script file */
	err = file_get_line(s);
    } else {
	/* interactive use */
	err = get_interactive_line(s);
    }

    return err;
}

/* allow for continuation of lines */

static int maybe_get_input_line_continuation (char *line)
{
    char tmp[MAXLINE];
    int contd, err = 0;

    if (!strncmp(line, "quit", 4)) {
	return 0;
    }

    contd = top_n_tail(line, MAXLINE, &err);

    while (contd && !err) {
	*tmp = '\0';

	if (batch || runit) {
	    char *test = fgets(tmp, MAXLINE, fb);

	    if (test == NULL) {
		break;
	    }
	} else {
#ifdef HAVE_READLINE
	    rl_gets(&line_read, "> ");
	    strcpy(tmp, line_read);
#else
	    fgets(tmp, MAXLINE, stdin); 
#endif
	}

	if (*tmp != '\0') {
	    if (strlen(line) + strlen(tmp) > MAXLINE - 1) {
		err = E_TOOLONG;
		break;
	    } else {
		strcat(line, tmp);
		compress_spaces(line);
	    }
	}
	contd = top_n_tail(line, MAXLINE, &err);
    }

    return err;
}

static int xout;

#ifdef HAVE_READLINE
static int ctrl_x (int count, int key)
{
    xout = 1;
    rl_done = 1;
    puts("exit");
    return 0;
}
#endif

static void handle_datafile (char *filearg, char *runfile,
			     DATASET *dset, PRN *prn,
			     PRN *cmdprn)
{
    char given_file[MAXLEN];
    int load_datafile = 1;
    int ftype, err = 0;

    strcpy(given_file, filearg);
    strcpy(datafile, filearg);

    ftype = detect_filetype(datafile, OPT_P);

    switch (ftype) {
    case GRETL_UNRECOGNIZED:
    case GRETL_NATIVE_DB:
    case GRETL_RATS_DB:
	exit(EXIT_FAILURE);
	break;
    case GRETL_XML_DATA:
    case GRETL_BINARY_DATA:
	err = gretl_read_gdt(datafile, dset, OPT_NONE, prn);
	break;
    case GRETL_CSV:
	err = import_csv(datafile, dset, OPT_NONE, prn);
	break;
    case GRETL_XLS:
    case GRETL_GNUMERIC:
    case GRETL_ODS:
	err = import_spreadsheet(datafile, ftype, NULL, NULL,
				 dset, OPT_NONE, prn);
	break;
    case GRETL_DTA:
    case GRETL_SAV:
    case GRETL_SAS:
    case GRETL_JMULTI:
    case GRETL_OCTAVE:
    case GRETL_WF1:
	err = import_other(datafile, ftype, dset, 
			   OPT_NONE, prn);
	break;
    case GRETL_SCRIPT:
	runit = 1;
	strcpy(runfile, datafile); 
	memset(datafile, 0, sizeof datafile);
	load_datafile = 0;
	break;
    default:
	break;
    }

    if (load_datafile) {
	if (err) {
	    errmsg(err, prn);
	    if (err == E_FOPEN) {
		show_paths();
	    }
	    exit(EXIT_FAILURE);
	}
	data_status = 1;
	if (!batch) { 
	    pprintf(cmdprn, "open %s\n", given_file);
	}
    }
}

static void check_help_file (void)
{
    const char *hpath = helpfile_path(GRETL_HELPFILE);
    FILE *fp = fopen(hpath, "r");

    if (fp != NULL) { 
	printf(_("\n\"help\" gives a list of commands\n"));
	fclose(fp);
    } else {
	printf(_("help file %s is not accessible\n"), hpath);
	show_paths();
    }
}

int main (int argc, char *argv[])
{
#ifdef WIN32
    char *callname = argv[0];
#endif
    char linecopy[MAXLINE];
    DATASET *dset = NULL;
    MODEL *model = NULL;
    ExecState state;
    char *line = NULL;
    int quiet = 0;
    int tool = 0;
    int pkgmode = 0;
    int load_datafile = 1;
    char filearg[MAXLEN];
    char runfile[MAXLEN];
    double scriptval = NADBL;
    CMD cmd;
    PRN *prn = NULL;
    PRN *cmdprn = NULL;
    int err = 0;

#ifdef G_OS_WIN32
    win32_set_gretldir(callname);
#endif

#ifdef ENABLE_NLS
    nls_init();
#endif

#ifdef HAVE_READLINE
    rl_bind_key(0x18, ctrl_x);
#endif

    dset = datainfo_new();
    if (dset == NULL) {
	noalloc();
    }

    if (argc < 2) {
	force_language(LANG_AUTO);
	load_datafile = 0;
    } else {
	gretlopt opt = 0;

	err = parse_options(&argc, &argv, &opt, &scriptval, filearg);

	if (!err && (opt & (OPT_DBOPEN | OPT_WEBDB))) {
	    /* catch GUI-only options */
	    err = E_BADOPT;
	}

	if (err) {
	    /* bad option */
	    usage(1);
	} else if (opt & (OPT_HELP | OPT_VERSION)) {
	    /* we'll exit in these cases */
	    if (opt & OPT_HELP) {
		usage(0);
	    } else {
		logo(0);
		exit(EXIT_SUCCESS);
	    }
	}
	    
	if (opt & (OPT_BATCH | OPT_RUNIT | OPT_MAKEPKG | OPT_INSTPKG)) {
	    if (*filearg == '\0') {
		/* we're missing a filename argument */
		fprintf(stdout, "No filename given\n");
		usage(1);
	    } else if ((opt & OPT_BATCH) && !strcmp(filearg, "-")) {
		/* batch mode, but read from stdin */
		quiet = batch_stdin = batch = 1;
		*runfile = '\0';
		load_datafile = 0;
	    } else {
		/* record argument (not a datafile) */
		strcpy(runfile, filearg);
		load_datafile = 0;
		if (opt & OPT_BATCH) {
		    batch = 1;
		} else if (opt & OPT_MAKEPKG) {
		    tool = quiet = batch = 1;
		    pkgmode = OPT_MAKEPKG;
		} else if (opt & OPT_INSTPKG) {
		    tool = quiet = batch = 1;
		    pkgmode = OPT_INSTPKG;
		} else {
		    runit = 1;
		}
	    }
	} else if (*filearg == '\0') {
	    load_datafile = 0;
	}

	if (opt & OPT_TOOL) {
	    tool = quiet = 1;
	    gretl_set_tool_mode();
	} else if (opt & OPT_QUIET) {
	    quiet = 1;
	}

	if (opt & OPT_ENGLISH) {
	    force_language(LANG_C);
	} else {
	    force_language(LANG_AUTO);
	}
    }

    libgretl_init();

    if (!tool) {
	logo(quiet);
	if (!quiet) {
	    session_time(NULL);
	}
    }

#if defined(OPENMP_BUILD) && !defined(WIN32) && !defined(OS_OSX)
    check_blas_threading(tool, quiet);
#endif    

    prn = gretl_print_new(GRETL_PRINT_STDOUT, &err);
    if (err) {
	noalloc();
    }

    line = malloc(MAXLINE);
    if (line == NULL) {
	noalloc();
    }

#ifdef WIN32
    win32_cli_read_rc(callname);
#else
    cli_read_rc();
#endif /* WIN32 */

    if (!batch && !tool) {
	strcpy(cmdfile, gretl_workdir());
	strcat(cmdfile, "session.inp");
	cmdprn = gretl_print_new_with_filename(cmdfile, &err);
	if (err) {
	    errmsg(err, prn);
	    return EXIT_FAILURE;
	}
    }

    if (load_datafile) {
	handle_datafile(filearg, runfile, dset, prn, cmdprn);
    }

    /* allocate memory for model */
    model = allocate_working_model();
    if (model == NULL) {
	noalloc(); 
    }

    gretl_cmd_init(&cmd);
    gretl_exec_state_init(&state, 0, line, &cmd, model, prn);
    set_debug_read_func(get_interactive_line);

    /* print list of variables */
    if (data_status) {
	list_series(dset, prn);
    }

    if (!na(scriptval)) {
	/* define "scriptopt" */
	gretl_scalar_add("scriptopt", scriptval);
    }

    /* misc. interactive-mode setup */
    if (!batch) {
	check_help_file();
	fb = stdin;
	push_input_file(fb);
	if (!runit && !data_status) {
	    fputs(_("Type \"open filename\" to open a data set\n"), stdout);
	}
    } else if (batch_stdin) {
	fb = stdin;
	push_input_file(fb);
    }	

#ifdef HAVE_READLINE
    initialize_readline();
#endif
    
    if (batch || runit) {
	/* re-initialize: will be incremented by "run" cmd */
	runit = 0;
	if (tool) {
	    set_gretl_echo(0);
	}
	if (*runfile != '\0' && pkgmode != OPT_INSTPKG) {
	    if (strchr(runfile, ' ')) {
		sprintf(line, "run \"%s\"", runfile);
	    } else {
		sprintf(line, "run %s", runfile);
	    }
	    err = cli_exec_line(&state, dset, cmdprn);
	    if (err && fb == NULL) {
		exit(EXIT_FAILURE);
	    }
	}
    }

    *linecopy = '\0';

    /* enter main command loop */
    
    while (cmd.ci != QUIT && fb != NULL && !xout) {
	if (err && gretl_error_is_fatal()) {
	    gretl_abort(linecopy);
	}

	if (gretl_execute_loop()) { 
	    if (gretl_loop_exec(&state, dset, NULL)) {
		return 1;
	    }
	} else {
	    err = cli_get_input_line(&state);
	    if (err) {
		errmsg(err, prn);
		break;
	    } else if (cmd.ci == QUIT) {
		/* no more input available */
		cli_exec_line(&state, dset, cmdprn);
		err = gretl_if_state_check(0);
		if (err) {
		    errmsg(err, prn);
		}		
		continue;
	    }
	}

	if (!state.in_comment) {
	    if (cmd.context == FOREIGN || cmd.context == MPI ||
		gretl_compiling_python(line)) {
		tailstrip(line);
	    } else {
		err = maybe_get_input_line_continuation(line); 
		if (err) {
		    errmsg(err, prn);
		    break;
		}
	    }
	} 

	strcpy(linecopy, line);
	tailstrip(linecopy);
	err = cli_exec_line(&state, dset, cmdprn);
    }

    /* finished main command loop */

    if (!err) {
	err = gretl_if_state_check(0);
	if (err) {
	    errmsg(err, prn);
	}
    }

    if (!err && pkgmode) {
	if (pkgmode == OPT_MAKEPKG) {
	    switch_ext(filearg, runfile, "gfn");
	    sprintf(line, "makepkg %s\n", filearg);
	} else {
	    if (!isalpha(filearg[0]) || filearg[1] == ':') {
		/* some sort of path: install local file */
		sprintf(line, "install %s --local\n", filearg);
	    } else {
		/* plain filename or http: install from server */
		sprintf(line, "install %s\n", filearg);
	    }
	}
	cli_exec_line(&state, dset, cmdprn);
    }

    /* leak check -- try explicitly freeing all memory allocated */

    destroy_working_model(model);
    destroy_dataset(dset);

    if (fb != stdin && fb != NULL) {
	fclose(fb);
    }

    free(line);

    gretl_print_destroy(prn);
    gretl_cmd_free(&cmd);
    libgretl_cleanup();

    return 0;
}

static void printline (const char *s)
{
    if (*s != '\0') {
	if (gretl_compiling_loop()) {
	    printf("> %s\n", s);
	} else {
	    printf("%s\n", s);
	}
    }
}

static void cli_exec_callback (ExecState *s, void *ptr,
			       GretlObjType type)
{
    int ci = s->cmd->ci;

    if (ci == MODELTAB || ci == GRAPHPG) {
	pprintf(s->prn, _("%s: command not available\n"), 
		gretl_command_word(ci));
    } else if (ci == OPEN) {
	char *fname = (char *) ptr;

	if (fname != NULL && *fname != '\0') {
	    strncpy(datafile, fname, MAXLEN - 1);
	}
	data_status = 1;
    }

    /* otherwise, no-op */
}

static int cli_renumber_series (const int *list,
				const char *parm,
				DATASET *dset,
				PRN *prn)
{
    int err, fixmax = highest_numbered_var_in_saved_object(dset);

    err = renumber_series_with_checks(list, parm, fixmax, dset, prn);
    if (err) {
	errmsg(err, prn);
    }

    return err;
}

static int cli_try_http (const char *s, char *fname, int *http)
{
    int err = 0;

    if (strncmp(s, "http://", 7) == 0 ||
	strncmp(s, "https://", 8) == 0) {
#ifdef USE_CURL
	err = retrieve_public_file(s, fname);
	if (!err) {
	    *http = 1;
	}
#else
	gretl_errmsg_set(_("Internet access not supported"));
	err = E_DATA;
#endif
    }

    return err;
}

static int cli_open_append (CMD *cmd, DATASET *dset, 
			    MODEL *model, PRN *prn)
{
    gretlopt opt = cmd->opt;
    PRN *vprn = prn;
    char newfile[MAXLEN] = {0};
    char response[3];
    int http = 0, dbdata = 0;
    int ftype;
    int err = 0;

    if (opt & OPT_K) {
	/* --frompkg=whatever */
	err = get_package_data_path(cmd->param, newfile);
	if (err) {
	    errmsg(err, prn);
	    return err;
	}
    } else {
	err = cli_try_http(cmd->param, newfile, &http);
	if (err) {
	    errmsg(err, prn);
	    return err;
	}

	if (!http && !(opt & OPT_O)) {
	    /* not using http or ODBC */
	    err = get_full_filename(cmd->param, newfile, (opt & OPT_W)?
				    OPT_W : OPT_NONE);
	    if (err) {
		errmsg(err, prn);
		return err;
	    }
	}
    }

    if (opt & OPT_W) {
	ftype = GRETL_NATIVE_DB_WWW;
    } else if (opt & OPT_O) {
	ftype = GRETL_ODBC;
    } else {
	ftype = detect_filetype(newfile, OPT_P);
    }

    dbdata = (ftype == GRETL_NATIVE_DB || ftype == GRETL_NATIVE_DB_WWW ||
	      ftype == GRETL_RATS_DB || ftype == GRETL_PCGIVE_DB ||
	      ftype == GRETL_ODBC);

    if (data_status && !batch && !dbdata && cmd->ci != APPEND &&
	strcmp(newfile, datafile)) {
	fprintf(stderr, _("Opening a new data file closes the "
			  "present one.  Proceed? (y/n) "));
	if (fgets(response, sizeof response, stdin) != NULL && 
	    *response != 'y' && *response != 'Y') {
	    pprintf(prn, _("OK, staying with current data set\n"));
	    return 0;
	}
    }

    if (!dbdata && cmd->ci != APPEND) {
	cli_clear_data(cmd, dset, model);
    }

    if (opt & OPT_Q) {
	/* --quiet, but in case we hit any problems below... */
	vprn = gretl_print_new(GRETL_PRINT_BUFFER, NULL);
    }

    if (ftype == GRETL_XML_DATA || ftype == GRETL_BINARY_DATA) {
	err = gretl_read_gdt(newfile, dset, opt, vprn);
    } else if (ftype == GRETL_CSV) {
	err = import_csv(newfile, dset, opt, vprn);
    } else if (SPREADSHEET_IMPORT(ftype)) {
	err = import_spreadsheet(newfile, ftype, cmd->list, cmd->parm2,
				 dset, opt, vprn);
    } else if (OTHER_IMPORT(ftype)) {
	err = import_other(newfile, ftype, dset, opt, vprn);
    } else if (ftype == GRETL_ODBC) {
	err = set_odbc_dsn(cmd->param, vprn);
    } else if (dbdata) {
	err = set_db_name(newfile, ftype, vprn);
    } else {
	err = gretl_get_data(newfile, dset, opt, vprn);
    }

    if (vprn != prn) {
	if (err) {
	    /* The user asked for --quiet operation, but something
	       went wrong so let's print any info we got on
	       vprn.
	    */
	    const char *buf = gretl_print_get_buffer(vprn);

	    if (buf != NULL && *buf != '\0') {
		pputs(prn, buf);
	    }
	} else {
	    /* print minimal success message */
	    pprintf(prn, _("Read datafile %s\n"), newfile);
	}
	gretl_print_destroy(vprn);
    }

    if (err) {
	errmsg(err, prn);
	return err;
    }

    if (!dbdata && !http && cmd->ci != APPEND) {
	strncpy(datafile, newfile, MAXLEN - 1);
    }

    data_status = 1;

    if (dset->v > 0 && !dbdata && !(opt & OPT_Q)) {
	list_series(dset, prn);
    }

    if (http) {
	remove(newfile);
    }

    return err;
}

static void maybe_save_session_output (const char *cmdfile)
{
    char outfile[FILENAME_MAX];

    printf(_("type a filename to store output (enter to quit): "));

    *outfile = '\0';

    if (fgets(outfile, sizeof outfile, stdin) != NULL) {
	top_n_tail(outfile, 0, NULL);
    }

    if (*outfile != '\0' && *outfile != '\n' && *outfile != '\r' 
	&& strcmp(outfile, "q")) {
	const char *udir = gretl_workdir();
	char *syscmd;

	printf(_("writing session output to %s%s\n"), udir, outfile);
#ifdef WIN32
	syscmd = gretl_strdup_printf("\"%sgretlcli\" -b \"%s\" > \"%s%s\"", 
				     gretl_home(), cmdfile, udir, outfile);
	system(syscmd);
#else
	syscmd = gretl_strdup_printf("gretlcli -b \"%s\" > \"%s%s\"", 
				     cmdfile, udir, outfile);
	gretl_spawn(syscmd);
#endif
	printf("%s\n", syscmd);
	free(syscmd);
    }
}

#define ENDRUN (NC + 1)

/* cli_exec_line: this is called to execute both interactive and
   script commands.  Note that most commands get passed on to the
   libgretl function gretl_cmd_exec(), but some commands that require
   special action are dealt with here.

   see also gui_exec_line() in gui2/library.c
*/

static int cli_exec_line (ExecState *s, DATASET *dset, PRN *cmdprn)
{
    char *line = s->line;
    CMD *cmd = s->cmd;
    PRN *prn = s->prn;
    MODEL *model = s->model;
    int old_runit = runit;
    char runfile[MAXLEN];
    int renumber = 0;
    int err = 0;

#if 0
    fprintf(stderr, "cli_exec_line: '%s'\n", line);
#endif

    if (gretl_compiling_function()) {
	err = gretl_function_append_line(line);
	if (err) {
	    errmsg(err, prn);
	} else {
	    pprintf(cmdprn, "%s\n", line);
	}
	return err;
    }

    if (string_is_blank(line)) {
	if (gretl_echo_space()) {
	    pputc(prn, '\n');
	}
	return 0;
    }

    if (!gretl_compiling_loop() && !s->in_comment &&
	!cmd->context && !gretl_if_state_false()) {
	/* catch requests relating to saved objects, which are not
	   really "commands" as such */
	int action = cli_saved_object_action(line, dset, prn);

	if (action == OBJ_ACTION_INVALID) {
	    return 1; /* action was faulty */
	} else if (action != OBJ_ACTION_NONE) {
	    return 0; /* action was OK (and handled), or ignored */
	}
    }

    /* tell libgretl if we're in batch mode */
    gretl_set_batch_mode(batch);

    if (gretl_compiling_loop()) {
	/* if we're stacking commands for a loop, parse "lightly" */
	err = get_command_index(line, LOOP, cmd);
    } else {
	err = parse_command_line(line, cmd, dset, NULL);
    }

    if (err) {
	int catch = 0;

	gretl_exec_state_uncomment(s);
	if (err != E_ALLOC && (cmd->flags & CMD_CATCH)) {
	    set_gretl_errno(err);
	    catch = 1;
	}
	gretl_echo_command(cmd, line, prn);
        errmsg(err, prn);
	return (catch)? 0 : err;
    }

    gretl_exec_state_transcribe_flags(s, cmd);

    /* if in batch mode, echo comments from input */
    if (batch && runit < 2 && cmd->ci == CMD_COMMENT && gretl_echo_on()) {
	printline(linebak);
    }

    if (cmd->ci < 0) {
	/* nothing there, comment, or masked by "if" */
	return 0;
    }

    if (s->sys != NULL && cmd->ci != END && cmd->ci != EQUATION &&
	cmd->ci != SYSTEM) {
	printf(_("Command '%s' ignored; not valid within equation system\n"), 
	       line);
	equation_system_destroy(s->sys);
	s->sys = NULL;
	return 1;
    }

    if (cmd->ci == LOOP && !batch && !runit) {
	pputs(prn, _("Enter commands for loop.  "
		     "Type 'endloop' to get out\n"));
    }

    if (cmd->ci == LOOP || gretl_compiling_loop()) {  
	/* accumulating loop commands */
	if (gretl_echo_on() && (!gretl_compiling_loop() || batch || runit)) {
	    /* straight visual echo */
	    gretl_echo_command(cmd, line, prn);
	}
	err = gretl_loop_append_line(s, dset);
	if (err) {
	    errmsg(err, prn);
	} else if (!batch && !runit) {
	    gretl_record_command(cmd, line, cmdprn);
	}
	return err;
    }

    if (gretl_echo_on()) {
	/* visual feedback, not recording */
	if (cmd->ci == FUNC && runit > 1) {
	    ; /* don't echo */
	} else if (batch || runit) {
	    gretl_echo_command(cmd, line, prn);
	}
    }

    check_for_loop_only_options(cmd->ci, cmd->opt, prn);

    gretl_exec_state_set_callback(s, cli_exec_callback, OPT_NONE);

    switch (cmd->ci) {

    case DELEET:
	err = gretl_delete_variables(cmd->list, cmd->param,
				     cmd->opt, dset, &renumber,
				     prn);
	if (err) {
	    errmsg(err, prn);
	} else if (renumber && !batch) {
	    pputs(prn, _("Take note: variables have been renumbered"));
	    pputc(prn, '\n');
	    maybe_list_series(dset, prn);
	}
	break;

    case HELP:
	cli_help(cmd->param, cmd->parm2, cmd->opt, prn);
	break;

    case OPEN:
    case APPEND:
	err = cli_open_append(cmd, dset, model, prn);
	break;

    case NULLDATA:
	if (cmd->order < 1) {
	    err = 1;
	    pputs(prn, _("Data series length count missing or invalid\n"));
	} else {
	    cli_clear_data(cmd, dset, model);
	    err = open_nulldata(dset, data_status, cmd->order, 
				cmd->opt, prn);
	    if (err) { 
		errmsg(err, prn);
	    } else {
		data_status = 1;
	    }
	}
	break;

    case QUIT:
	if (runit || batch_stdin) {
	    *s->runfile = '\0';
	    runit--;
	    fclose(fb);
	    fb = pop_input_file();
	    if (fb == NULL) {
		if (gretl_messages_on()) {
		    pputs(prn, _("Done\n"));
		}
	    } else {
		cmd->ci = ENDRUN;
	    }
	} else {
	    printf(_("commands saved as %s\n"), cmdfile);
	    gretl_print_destroy(cmdprn);
	    if (!(cmd->opt & OPT_X)) {
		maybe_save_session_output(cmdfile);
	    }
	}
	break;

    case RUN:
    case INCLUDE:
	if (cmd->ci == INCLUDE) {
	    err = get_full_filename(cmd->param, runfile, OPT_I);
	} else {
	    err = get_full_filename(cmd->param, runfile, OPT_S);
	}
	if (err) { 
	    errmsg(err, prn);
	    break;
	}
	if (gretl_messages_on()) {
	    pprintf(prn, " %s\n", runfile);
	}
	if (cmd->ci == INCLUDE && gretl_is_xml_file(runfile)) {
	    err = load_user_XML_file(runfile, prn);
	    if (err) {
		pprintf(prn, _("Error reading %s\n"), runfile);
	    } else {
		pprintf(cmdprn, "include \"%s\"\n", runfile);
	    }
	    break;
	} else if (cmd->ci == INCLUDE && gfn_is_loaded(runfile)) {
	    break;
	}
	if (!strcmp(runfile, s->runfile)) { 
	    pprintf(prn, _("Infinite loop detected in script\n"));
	    err = 1;
	    break;
	}
	if (fb != NULL) {
	    push_input_file(fb);
	}
	if ((fb = fopen(runfile, "r")) == NULL) {
	    pprintf(prn, _("Error reading %s\n"), runfile);
	    err = 1;
	    fb = pop_input_file();
	} else {
	    gretl_set_current_dir(runfile);
	    strcpy(s->runfile, runfile);
	    if (cmd->ci == INCLUDE) {
		pprintf(cmdprn, "include \"%s\"\n", runfile);
	    } else {
		pprintf(cmdprn, "run \"%s\"\n", runfile);
	    }
	    runit++;
	}
	break;

    case CLEAR:
	err = cli_clear_data(cmd, dset, model);
	break;

    case DATAMOD:
	if (cmd->auxint == DS_CLEAR) {
	    err = cli_clear_data(cmd, dset, model);
	    pputs(prn, _("Dataset cleared\n"));
	    break;
	} else if (cmd->auxint == DS_RENUMBER) {
	    err = cli_renumber_series(cmd->list, cmd->parm2, dset, prn);
	    break;
	}
	/* else fall-through intended */

    default:
	err = gretl_cmd_exec(s, dset);
	break;
    }

    if (!err && cmd->ci != QUIT && gretl_echo_on() && !batch && !old_runit) {
	/* record a successful interactive command */
	gretl_record_command(cmd, line, cmdprn);
    }

    if (err) {
	gretl_exec_state_uncomment(s);
    }

    return err;
}

/* apparatus for keeping track of input stream */

#define N_STACKED_FILES 8

static int nfiles;
static FILE *fstack[N_STACKED_FILES];

static int push_input_file (FILE *fp)
{
    int err = 0;

    if (nfiles >= N_STACKED_FILES) {
	err = 1;
    } else {
	fstack[nfiles++] = fp;
    }

    return err;
}

static FILE *pop_input_file (void)
{
    FILE *ret = NULL;

    if (nfiles > 0) {
	ret = fstack[--nfiles];
    }

    return ret;
}

#include "cli_object.c"

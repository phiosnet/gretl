#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef G_OS_WIN32
int read_win32_config (int debug);
#else
int gretl_config_init (void);
#endif

#ifdef HAVE_TRAMO
int get_tramo_ok (void);
#endif

#ifdef HAVE_X12A
int get_x12a_ok (void);
#endif

void set_gretl_startdir (void);

int using_hc_by_default (void);

int get_manpref (void);

int autoicon_on (void);

void write_rc (void);

void dump_rc (void);

void force_english_help (void);

int options_dialog (int page, const char *varname, GtkWidget *parent);

void font_selector (GtkAction *action);

void set_fixed_font (void);

void update_persistent_graph_colors (void);

void update_persistent_graph_font (void);

void set_app_font (const char *fontname);

const char *get_app_fontname (void);

void get_default_dir (char *s, int action);

int gui_set_working_dir (char *dirname);

void set_working_dir_callback (GtkWidget *w, char *path);

void working_dir_dialog (void);

void set_path_callback (char *setvar, char *setting);

void set_datapage (const char *str);

void set_scriptpage (const char *str);

const char *get_datapage (void);

const char *get_scriptpage (void);

int check_for_prog (const char *prog);

#endif /* SETTINGS_H */

/* The one table of exporter types (B3). boot.c lists every exporter once, as an ExpType row; this module is the only place that walks the
   list: the boot-time settings.ini keys (export_models = all ...), the "Prepare modding files" row of the System tab (and --export-all),
   the atexit shutdown. Adding an exporter is one row in the table, plus its own module; nothing else names it.
   No SDL and no game: the rows' functions are the exporters' own. */
#ifndef EXP_TYPES_H
#define EXP_TYPES_H

typedef struct ExpType {
    const char *key;  /* settings.ini [data] key, "export_models" */
    const char *name; /* the step's name in the System tab's row ("Models"): "Models 3/14" */
    const char *note; /* the comment a save writes above the key: lines starting "; ", each ended by "\n" */
    const char *dflt; /* the value when the key is absent: NULL = nothing is exported at start; "off" is always nothing */
    /* the exporter's own setup: hands it the data folder (NULL: none; the setup must then leave it unusable) and the disc readers. NULL ok. */
    void (*setup)(const char *dataDir);
    /* the synchronous run of the settings.ini value: "all" or the exporter's own item words. Required. */
    int (*run)(const char *spec);
    /* called once after the settings.ini run (frees what only the run needed). NULL ok. */
    void (*after)(void);
    /* The on-demand job (all NULL together for an exporter that only runs at start): start queues spec ("all") on a worker thread, `force`
       rebuilds what is up to date; the row passes 0. Returns >0 queued, else nothing started. */
    int (*start)(const char *spec, int force);
    int (*pump)(unsigned budgetMs); /* one slice per frame: 1 while work remains */
    int (*active)(void);            /* 1 while the job runs */
    void (*counts)(int *done, int *failed, int *total);
    const char *(*status)(void); /* "Exporting 12/248", "Done (248)" */
    void (*shutdown)(void);      /* stops and joins the worker; run at exit */
} ExpType;

/* Registers the table (the order is the order the row runs them in; it must outlive the program). NULL, 0 unregisters. */
void expTypesSet(const ExpType *types, int count);
int expTypeCount(void);
const ExpType *expTypeAt(int i);
/* The type has an on-demand job (a step of the "Prepare modding files" row) */
int expTypeIsJob(const ExpType *t);
/* How many types have one: the row exists only when this is above 0 */
int expTypesJobCount(void);

/* Before the window opens: registers each key's comment with the settings file, runs every setup and arms the shutdown at exit. */
void expTypesSetup(const char *dataDir);
/* Runs the types whose settings.ini line is present (or that have a default), in table order, then their `after` hooks. No data folder: nothing. */
void expTypesRunSettings(const char *dataDir);

#endif

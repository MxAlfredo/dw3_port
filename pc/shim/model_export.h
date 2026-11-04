#ifndef PC_MODEL_EXPORT_H
#define PC_MODEL_EXPORT_H

/*
 * The original battle models as neutral sources (model-import I7, ADR-010 / ADR-011): for a fighter id, the model, its motions
 * and its texture are read from the disc and written as
 *
 *   <data>/models/<id>/model.gltf  model.bin  model.png  model.json   (model-src-v1, what model_bake.c reads)
 *
 * so a modder can open the glTF in Blender, edit it, and drop the folder unchanged into a mod's content/models/<key>/.
 * The glTF is at Blender size (1/1000: Kotemon, 1886 game units, is about 1.9 m tall) and model.json says "scale": 1000, which the baker undoes.
 *
 * The exporter is a port of tools/model_export.py (--source) and writes the same model.gltf, model.bin and model.json bytes (the
 * PNG has the same pixels; its deflate stream is the port's own). Portable C like the baker (ADR-009 section 7): the disc comes
 * in through a callback, nothing here knows the file layers. Compile with -ffp-contract=off. Only the menu's job thread (below) uses SDL, and only its threads and mutex.
 *
 * On demand only (all 248 are about 190 MB): the System tab's "Export models" row and the [data] export_models key of
 * settings.ini. The menu's job runs on a worker thread (modelExportStart) so the game keeps its frame rate; the settings.ini key at start runs synchronously; every file follows the rule of
 * the other data/ families (a manifest of what the port wrote; an edited file stays, the new one goes beside it as <file>.new).
 * The manifest also remembers, per fighter, the exporter version that wrote it: a request (the settings.ini key at start) skips a
 * fighter whose version is current and whose four files exist, without building it (an edited file counts as present). The menu
 * row forces a rebuild of all (modelExportRequestEx). Bump MODEL_EXPORT_VERSION (above) when the output bytes change.
 */

#include <stddef.h>
#include <stdint.h>

#define MODEL_EXPORT_FILE_FIGHTERS 0x1CC /* FILE_FIGHTERS in the EU version */
#define MODEL_EXPORT_UNIT_SCALE (1.0 / 1000.0)

/* Bump whenever the bytes the exporter writes change (the tests compare them with tools/model_export.py): a manifest that
   remembers an older version makes every fighter be built again once, and the baker's cache key of a model.json that starts
   from an original ("from", model_bake.h) follows it. */
#define MODEL_EXPORT_VERSION 1

/* The bytes of a file number as on the disc (NULL: it cannot be read). The exporter copies them before it returns from the call
   that asked, so the callback may reuse its buffer. */
typedef struct ModelExportIo {
    const uint8_t *(*file)(void *ud, int file, size_t *len);
    void *ud;
} ModelExportIo;

typedef struct ModelExportOut {
    uint8_t *gltf, *bin, *png, *json; /* malloc'ed */
    size_t gltfLen, binLen, pngLen, jsonLen;
    int bones, anims, tris;
} ModelExportOut;

/* The fighter ids of the fighters file, in its order; returns how many (up to cap), -1 when the file cannot be read */
int modelExportIds(const ModelExportIo *io, int *ids, int cap);
/* One fighter's four files in memory. 0, or -1 with the reason in err. unitScale is 1/1000 unless a test says otherwise */
int modelExportBuild(const ModelExportIo *io, int fighterId, double unitScale, ModelExportOut *out, char *err, size_t errCap);
void modelExportFree(ModelExportOut *o);

/* ---- the job: where the files go and which fighters ---- */

/* The data folder (<data>/models/...) and the disc reader. Call again to change them; an unfinished job is dropped */
void modelExportSetup(const char *dataDir, const ModelExportIo *io);
/* Queues "all" or fighter numbers separated by commas/spaces ("383, 12", "dw3:digimon/383" too). The queued count, or -1 when
   there is nothing to do (no data folder, no reader, nothing valid). A job that is running is replaced. */
int modelExportRequest(const char *spec);
/* The same; force != 0 rebuilds every queued fighter even when it is up to date */
int modelExportRequestEx(const char *spec, int force);
/* How many fighters of the last job were really built (the rest were skipped as up to date) */
int modelExportBuilt(void);
/* The menu's job: modelExportRequestEx, then a worker thread builds and writes the queue. The worker never calls the disc reader (it
   shares a FILE* with the game): the main thread's modelExportPump reads the files of the next fighters into memory for it.
   The queued count, or -1 (nothing to do, or a job is already running: it is left alone). Then call modelExportPump every frame */
int modelExportStart(const char *spec, int force);
/* Stops a running worker between two fighters and joins it (the manifest is saved); atexit-safe, a no-op without a job */
void modelExportShutdown(void);
/* Exports queued fighters for about budgetMs milliseconds (at least one). 1 while work remains, 0 when the job is over.
   With a worker job (modelExportStart) it only feeds the worker and joins it at the end; budgetMs is ignored */
int modelExportPump(unsigned budgetMs);
int modelExportActive(void);
/* Progress of the last job: done and failed so far, how many were queued */
void modelExportCounts(int *done, int *failed, int *total);
/* The row's value text: "" before any job, "Exporting 12/248", "Done (248)", "Done, 3 failed" (short: the row has little room; the log has the details) */
const char *modelExportStatus(void);
/* One fighter, now (the files are written under the manifest rule). 0, or -1 (logged) */
int modelExportOne(int fighterId);
/* The folder of a fighter relative to the data folder, "models/<id>/": what data/content/digimon.json names */
void modelExportFolder(int fighterId, char *out, size_t cap);

#endif

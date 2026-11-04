#ifndef PC_MODEL_BAKE_H
#define PC_MODEL_BAKE_H

/*
 * Models from a neutral source (model-import I5, ADR-011): a folder with model.json ("model-src-v1"), a glTF 2.0 file and its PNG
 * becomes the game's battle model archive when the game loads, in portable C (ADR-009 section 7).
 *
 *   mods/<mod>/content/models/<key>/model.json   registered as "<mod>:<key>"
 *
 * The converter (modelBake) is a port of tools/model_import.py and gives the same bytes; the registry (modelsFile) bakes a
 * registered source on first use, keeps the blob in <user>/cache/models/ under a hash of the sources and the baker's version
 * (a later start reads it back instead of converting), and adds it as a virtual file (port_fs.h). A source that cannot be
 * baked is logged as "[models] <name> rejected: <why>" and its Digimon keeps the template model. Nothing here reads the disc.
 *
 * "from" (model-polish P3): a model.json may say "from": "dw3:model/<fighter id>" and then ship only what it changes. The original
 * fighter is exported in memory (model_export.h, the same files as data/models/<id>/) through the ModelOriginals callback, the
 * original's model.json is the base and the mod's keys replace its top-level keys (shallow; "motions" is merged slot by slot, the
 * mod's slot winning), and a file the mod's folder lacks (model.gltf, model.bin, model.png) is the original's. A file the mod
 * ships wins, so a model.png of its own must keep the original's UV layout (the exported glTF names "model.png"). The cache key
 * covers the mod's files, the original's id and MODEL_EXPORT_VERSION, not the original's bytes: a cache hit never builds the
 * export. "from" here is the model's; digimon.json's own "from" (the template fighter) is another thing.
 */

#include <stddef.h>
#include <stdint.h>

#include "model_export.h"

/* Bump when the converter's output changes: it is part of the cache key */
#define MODEL_BAKE_VERSION 1

/* 0: information, 1: a warning (the same ones the tool prints) */
typedef void (*ModelLogFn)(void *ud, int warning, const char *msg);

typedef struct ModelCacheHooks {
    /* a baked blob for the hash of the sources: malloc'ed, 1 when found */
    int (*get)(void *ud, uint64_t key, uint8_t **blob, size_t *len);
    void (*put)(void *ud, uint64_t key, const uint8_t *blob, size_t len);
    void *ud;
} ModelCacheHooks;

typedef struct ModelBaked {
    uint8_t *blob; /* malloc'ed: the archive (entry 0 motions, 1 model, 2 texture, ...) */
    size_t len;
    int cached;    /* the blob came from the cache hooks */
    uint64_t key;  /* the hash of the sources and the baker version */
    int bones, polys, clips, slots, islands, palettes;
    double rms;
} ModelBaked;

/* Where "from" gets an original fighter's four files (modelExportBuild over the disc reader, on the calling thread). 0 and `out`
   filled (the baker frees it), or -1 with the reason in err */
typedef struct ModelOriginals {
    int (*build)(void *ud, int fighterId, ModelExportOut *out, char *err, size_t errCap);
    void *ud;
} ModelOriginals;

/* Bakes <dir>/<metaFile> (a model-src-v1 JSON; its glTF and PNG paths are relative to dir). 0 and `out` filled, or -1 and the reason
   in err. cache may be NULL. A model.json with "from" is rejected here: use modelBakeFrom. */
int modelBake(const char *dir, const char *metaFile, ModelLogFn log, void *ud, const ModelCacheHooks *cache, ModelBaked *out, char *err,
              size_t errCap);
/* The same with originals: a model.json with "from" is completed from them (NULL: such a model is rejected, "needs the disc") */
int modelBakeFrom(const char *dir, const char *metaFile, ModelLogFn log, void *ud, const ModelCacheHooks *cache, const ModelOriginals *orig,
                  ModelBaked *out, char *err, size_t errCap);
void modelBakedFree(ModelBaked *m);

/* What a Digimon takes from the source's model.json without baking it: the template and effects donor as original fighter ids (-1:
   not given), "info" distance and height (INT_MIN: not given) */
typedef struct ModelSrcInfo {
    int templateId, effectsId, distance, height;
} ModelSrcInfo;
int modelSourceInfo(const char *dir, const char *metaFile, ModelSrcInfo *out, char *err, size_t errCap);
/* The same through "from": what the mod's model.json lacks comes from the original's (built only when something is missing) */
int modelSourceInfoFrom(const char *dir, const char *metaFile, const ModelOriginals *orig, ModelSrcInfo *out, char *err, size_t errCap);

/* ---- the registry of the active mods: "<mod>:<key>" is mods/<mod>/content/models/<key>/model.json ---- */
int modelsHas(const char *name);                          /* 1 when a source of that name exists */
int modelsInfo(const char *name, ModelSrcInfo *out);      /* 1 and the info; 0 (logged) when it cannot be read */
int modelsFile(const char *name);                         /* the virtual file number of the baked archive, -1 (logged) when it is rejected */
void modelsSetOriginals(const ModelOriginals *orig);      /* the originals for "from" (copied; NULL: none), set once at start */
void modelsReset(void);                                   /* forget the baked files (after pfsClearVirtualFiles) */

#endif

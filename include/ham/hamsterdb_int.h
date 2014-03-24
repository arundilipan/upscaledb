/*
 * Copyright (C) 2005-2014 Christoph Rupp (chris@crupp.de).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file hamsterdb_int.h
 * @brief Internal hamsterdb Embedded Storage PRO functions.
 * @author Christoph Rupp, chris@crupp.de
 *
 * Please be aware that the interfaces in this file are mostly for internal
 * use. Unlike those in hamsterdb.h they are not stable and can be changed
 * with every new version.
 *
 */

#ifndef HAM_HAMSTERDB_INT_H__
#define HAM_HAMSTERDB_INT_H__

#include <ham/hamsterdb.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ham_extended_api hamsterdb Enhanced API
 * @{
 */

/** get the (non-persisted) flags of a key */
#define ham_key_get_intflags(key)       (key)->_flags

/**
 * set the flags of a key
 *
 * Note that the ham_find/ham_cursor_find/ham_cursor_find_ex flags must
 * be defined such that those can peacefully co-exist with these; that's
 * why those public flags start at the value 0x1000 (4096).
 */
#define ham_key_set_intflags(key, f)    (key)->_flags=(f)

/**
 * Verifies the integrity of the Database
 *
 * This function is only interesting if you want to debug hamsterdb.
 *
 * @param db A valid Database handle
 * @param flags Optional flags for the integrity check, combined with
 *      bitwise OR. Possible flags are:
 *    <ul>
 *     <li>@ref HAM_PRINT_GRAPH</li> Prints the Btree as a graph; stores
 *      the image as "graph.png" in the current working directory. It uses
 *      the "dot" tool from graphviz to generate the image.
 *      This functionality is only available in DEBUG builds!
 *    </ul>
 *
 * @return @ref HAM_SUCCESS upon success
 * @return @ref HAM_INTEGRITY_VIOLATED if the Database is broken
 */
HAM_EXPORT ham_status_t HAM_CALLCONV
ham_db_check_integrity(ham_db_t *db, ham_u32_t flags);

/** Flag for ham_db_check_integrity */
#define HAM_PRINT_GRAPH             1

/**
 * Set a user-provided context pointer
 *
 * This function sets a user-provided context pointer. This can be any
 * arbitrary pointer; it is stored in the Database handle and can be
 * retrieved with @a ham_get_context_data. It is mainly used by Wrappers
 * and language bindings.
 *
 * @param db A valid Database handle
 * @param data The pointer to the context data
 */
HAM_EXPORT void HAM_CALLCONV
ham_set_context_data(ham_db_t *db, void *data);

/**
 * Retrieves a user-provided context pointer
 *
 * This function retrieves a user-provided context pointer. This can be any
 * arbitrary pointer which was previously stored with @a ham_set_context_data.
 *
 * @param db A valid Database handle
 * @param dont_lock Whether the Environment mutex should be locked or not
 *      this is used to avoid recursive locks when retrieving the context
 *      data in a compare function
 *
 * @return The pointer to the context data
 */
HAM_EXPORT void * HAM_CALLCONV
ham_get_context_data(ham_db_t *db, ham_bool_t dont_lock);

/**
 * Retrieves the Database handle of a Cursor
 *
 * @param cursor A valid Cursor handle
 *
 * @return @a The Database handle of @a cursor
 */
HAM_EXPORT ham_db_t * HAM_CALLCONV
ham_cursor_get_database(ham_cursor_t *cursor);

/**
 * Retrieves collected metrics from the hamsterdb Environment. Used mainly
 * for testing.
 * See below for the structure with the currently available metrics.
 * This structure will change a lot; the first field is a version indicator
 * that applications can use to verify that the structure layout is compatible.
 *
 * These metrics are NOT persisted to disk.
 *
 * Metrics marked "global" are stored globally and shared between multiple
 * Environments.
 */
#define HAM_METRICS_VERSION         8

typedef struct ham_env_metrics_t {
  // the version indicator - must be HAM_METRICS_VERSION
  ham_u16_t version;

  // number of total allocations for the whole lifetime of the process
  ham_u64_t mem_total_allocations;

  // currently active allocations for the whole process
  ham_u64_t mem_current_allocations;

  // current amount of memory allocated and tracked by the process
  // (excludes memory used by the kernel or not allocated with
  // malloc/free)
  ham_u64_t mem_current_usage;

  // peak usage of memory (for the whole process)
  ham_u64_t mem_peak_usage;

  // the heap size of this process
  ham_u64_t mem_heap_size;

  // amount of pages fetched from disk
  ham_u64_t page_count_fetched;

  // amount of pages written to disk
  ham_u64_t page_count_flushed;

  // number of index pages in this Environment
  ham_u64_t page_count_type_index;

  // number of blob pages in this Environment
  ham_u64_t page_count_type_blob;

  // number of page-manager pages in this Environment
  ham_u64_t page_count_type_page_manager;

  // number of successful freelist hits
  ham_u64_t freelist_hits;

  // number of freelist misses
  ham_u64_t freelist_misses;

  // number of successful cache hits
  ham_u64_t cache_hits;

  // number of cache misses
  ham_u64_t cache_misses;

  // number of blobs allocated
  ham_u64_t blob_total_allocated;

  // number of blobs read
  ham_u64_t blob_total_read;

  // (global) number of btree page splits
  ham_u64_t btree_smo_split;

  // (global) number of btree page merges
  ham_u64_t btree_smo_merge;

  // (global) number of extended keys
  ham_u64_t extended_keys;

  // (global) number of extended duplicate tables
  ham_u64_t extended_duptables;

  // number of bytes that the log/journal flushes to disk
  ham_u64_t journal_bytes_flushed;

  // PRO: log/journal bytes before compression
  ham_u64_t journal_bytes_before_compression;

  // PRO: log/journal bytes after compression
  ham_u64_t journal_bytes_after_compression;

  // PRO: record bytes before compression
  ham_u64_t record_bytes_before_compression;

  // PRO: record bytes after compression
  ham_u64_t record_bytes_after_compression;

  // PRO: key bytes before compression
  ham_u64_t key_bytes_before_compression;

  // PRO: key bytes after compression
  ham_u64_t key_bytes_after_compression;

  // PRO: set to the max. SIMD lane width (0 if SIMD is not available)
  int simd_lane_width;

  // PRO: set to true if AVX is enabled
  ham_bool_t is_avx_enabled;

} ham_env_metrics_t;

/**
 * Retrieves the current metrics from an Environment
 */
HAM_EXPORT ham_status_t HAM_CALLCONV
ham_env_get_metrics(ham_env_t *env, ham_env_metrics_t *metrics);

/**
 * Returns @ref HAM_TRUE if this hamsterdb library was compiled with debug
 * diagnostics, checks and asserts
 */
HAM_EXPORT ham_bool_t HAM_CALLCONV
ham_is_debug();

/**
 * Returns @ref HAM_TRUE if this hamsterdb library is the commercial
 * closed-source "hamsterdb pro" edition
 */
HAM_EXPORT ham_bool_t HAM_CALLCONV
ham_is_pro();

/**
 * Returns the end time of the evaluation period, if this is an evaluation
 * license of the commercial closed-source "hamsterdb pro";
 * returns 0 otherwise
 */
HAM_EXPORT ham_u32_t HAM_CALLCONV
ham_is_pro_evaluation();

/**
 * @}
 */

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* HAM_HAMSTERDB_INT_H__ */

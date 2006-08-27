/**
 * Copyright 2005, 2006 Christoph Rupp (chris@crupp.de)
 * see file LICENSE for license and copyright information
 *
 *
 */

#include <string.h>
#include <ham/hamsterdb.h>
#include <ham/config.h>
#include "error.h"
#include "mem.h"
#include "db.h"
#include "version.h"
#include "txn.h"
#include "keys.h"
#include "cache.h"
#include "blob.h"
#include "freelist.h"

/*
 * default callback function which dumps a key
 */
static void
my_dump_cb(const ham_u8_t *key, ham_size_t keysize)
{
    ham_size_t i, limit;

    if (keysize>16)
        limit=16;
    else
        limit=keysize;

    for (i=0; i<limit; i++)
        printf("%02x ", key[i]);

    if (keysize>limit)
        printf("... (%d more bytes)\n", keysize-limit);
    else
        printf("\n");
}

const char *
ham_strerror(ham_status_t result)
{
    switch (result) {
        case HAM_SUCCESS: 
            return ("Success");
        case HAM_SHORT_READ: 
            return ("Short read");
        case HAM_SHORT_WRITE: 
            return ("Short write");
        case HAM_INV_KEYSIZE: 
            return ("Invalid key size");
        case HAM_INV_PAGESIZE: 
            return ("Invalid page size");
        case HAM_DB_ALREADY_OPEN: 
            return ("Db already open");
        case HAM_OUT_OF_MEMORY: 
            return ("Out of memory");
        case HAM_INV_BACKEND:
            return ("Invalid backend");
        case HAM_INV_PARAMETER:
            return ("Invalid parameter");
        case HAM_INV_FILE_HEADER:
            return ("Invalid database file header");
        case HAM_INV_FILE_VERSION:
            return ("Invalid database file version");
        case HAM_KEY_NOT_FOUND:
            return ("Key not found");
        case HAM_DUPLICATE_KEY:
            return ("Duplicate key");
        case HAM_INTEGRITY_VIOLATED:
            return ("Internal integrity violated");
        case HAM_INTERNAL_ERROR:
            return ("Internal error");
        case HAM_DB_READ_ONLY:
            return ("Database opened read only");
        case HAM_BLOB_NOT_FOUND:
            return ("Data blob not found");
        case HAM_PREFIX_REQUEST_FULLKEY:
            return ("Comparator needs more data");

        /* fall back to strerror() */
        default: 
            return (strerror(result));
    }
}

ham_status_t
ham_new(ham_db_t **db)
{
    /* allocate memory for the ham_db_t-structure */
    *db=(ham_db_t *)ham_mem_alloc(sizeof(ham_db_t));
    if (!(*db)) 
        return (HAM_OUT_OF_MEMORY);

    /* reset the whole structure */
    memset(*db, 0, sizeof(ham_db_t));
    db_set_fd((*db), -1);
    return (0);
}

ham_status_t
ham_delete(ham_db_t *db)
{
    /* free cached data pointers */
    if (db_get_record_allocdata(db))
        ham_mem_free(db_get_record_allocdata(db));

    /* close the backend */
    if (db_get_backend(db)) {
        ham_backend_t *be=db_get_backend(db);
        be->_fun_delete(be);
        ham_mem_free(be);
    }

    /* get rid of the header page */
    if (db_get_header_page(db))
        db_free_page_struct(db_get_header_page(db));

    /* get rid of the cache */
    if (db_get_cache(db)) {
        cache_delete(db_get_cache(db));
        db_set_cache(db, 0);
    }

    /* free all remaining memory */
    ham_mem_free(db);

    return (0);
}

ham_status_t
ham_open(ham_db_t *db, const char *filename, ham_u32_t flags)
{
    ham_fd_t fd;
    ham_status_t st;
    ham_cache_t *cache;
    ham_backend_t *backend;
    ham_u8_t hdrbuf[512];
    db_header_t *dbhdr;
    ham_page_t *page;

    /* cannot open an in-memory-db */
    if (flags&HAM_IN_MEMORY_DB)
        return (HAM_INV_PARAMETER);

    /* 
     * TODO mmap ist eingeschaltet
    if (!(flags&HAM_IN_MEMORY_DB))
        flags|=DB_USE_MMAP;
     */

    /* open the file */
    st=os_open(filename, flags, &fd);
    if (st) {
        ham_log("os_open of %s failed with status %d (%s)", filename, 
                st, ham_strerror(st));
        db_set_error(db, st);
        return (st);
    }

    /* initialize the database handle */
    db_set_fd(db, fd);
    db_set_error(db, 0);

    /* 
     * read the database header 
     *
     * !!!
     * now this is an ugly problem - the database header is one page, but
     * how large is one page? chances are good that it's the default 
     * page-size, but we really can't be sure.
     *
     * read 512 byte (the minimum page size) and 
     * extract the "real" page size, then read the real page. 
     * (but i really don't like this)
     */
    st=os_read(fd, hdrbuf, sizeof(hdrbuf));
    if (st) {
        ham_log("os_read of %s failed with status %d (%s)", filename, 
                st, ham_strerror(st));
        db_set_error(db, st);
        return (st);
    }
    dbhdr=(db_header_t *)&hdrbuf[12];
    db_set_pagesize(db, dbhdr->_pagesize);

    /* 
     * now allocate and read the header page 
     */
    page=db_alloc_page_struct(db);
    if (!page)
        return (db_get_error(db));
    st=db_fetch_page_from_device(page, 0);
    if (st) 
        return (st);
    page_set_type(page, PAGE_TYPE_HEADER);
    db_set_header_page(db, page);

    /*
     * copy the persistent header to the database object
     */
    memcpy(&db_get_header(db), page_get_payload(page), 
            sizeof(db_header_t)-sizeof(freel_payload_t));

    /* check the file magic */
    if (db_get_magic(db, 0)!='H' || 
        db_get_magic(db, 1)!='A' || 
        db_get_magic(db, 2)!='M' || 
        db_get_magic(db, 3)!='\0') {
        ham_log("invalid file type - %s is not a hamster-db", filename);
        db_set_error(db, HAM_INV_FILE_HEADER);
        return (HAM_INV_FILE_HEADER);
    }

    /* check the database version */
    if (db_get_version(db, 0)!=HAM_VERSION_MAJ || 
        db_get_version(db, 1)!=HAM_VERSION_MIN) {
        ham_log("invalid file version", 0);
        db_set_error(db, HAM_INV_FILE_VERSION);
        return (HAM_INV_FILE_VERSION);
    }

    /* create the backend */
    backend=db_create_backend(db, flags);
    if (!backend) {
        ham_log("unable to create backend with flags 0x%x", flags);
        db_set_error(db, HAM_INV_BACKEND);
        return (HAM_INV_BACKEND);
    }
    db_set_backend(db, backend);

    /* initialize the backend */
    st=backend->_fun_open(backend, flags);
    if (st) {
        ham_log("backend create() failed with status %d (%s)", 
                st, ham_strerror(st));
        db_set_error(db, st);
        return (st);
    }

    /* initialize the cache */
    cache=cache_new(db, 0, HAM_DEFAULT_CACHESIZE);
    if (!cache)
        return (db_get_error(db));
    db_set_cache(db, cache);

    /* create the freelist */
    st=freel_create(db);
    if (st) {
        ham_log("unable to create freelist", 0);
        return (st);
    }

    /* set the key compare function */
    ham_set_compare_func(db, db_default_compare);
    ham_set_prefix_compare_func(db, 0); /* TODO */

    return (HAM_SUCCESS);
}

ham_status_t
ham_create(ham_db_t *db, const char *filename, ham_u32_t flags, ham_u32_t mode)
{
    return (ham_create_ex(db, filename, flags, mode, 0, 0, 
                HAM_DEFAULT_CACHESIZE));
}

ham_status_t
ham_create_ex(ham_db_t *db, const char *filename, 
        ham_u32_t flags, ham_u32_t mode, ham_u16_t pagesize, 
        ham_u16_t keysize, ham_size_t cachesize)
{
    ham_fd_t fd;
    ham_status_t st;
    ham_cache_t *cache;
    ham_backend_t *backend;

    /* 
     * TODO mmap ist eingeschaltet
    if (!(flags&HAM_IN_MEMORY_DB))
        flags|=DB_USE_MMAP;
     */

    if (keysize==0)
        keysize=32-sizeof(key_t)-1;

    /*
     * make sure that the pagesize is aligned to 512k and that 
     * a page is big enough for at least 4 keys
     */
    if (pagesize==0) 
        pagesize=HAM_DEFAULT_PAGESIZE;
    else if (pagesize%512)
        return (HAM_INV_PAGESIZE);
    if (pagesize/keysize<4)
        return (HAM_INV_KEYSIZE);
    
    /*
     * initialize the header
     */
    db_set_magic(db, 'H', 'A', 'M', '\0');
    db_set_version(db, HAM_VERSION_MAJ, HAM_VERSION_MIN, HAM_VERSION_REV, 0);
    db_set_serialno(db, HAM_SERIALNO);
    db_set_flags(db, flags);
    db_set_error(db, HAM_SUCCESS);
    db_set_pagesize(db, pagesize);
    db_set_keysize(db, keysize);

    /* initialize the cache */
    cache=cache_new(db, flags, cachesize);
    if (!cache)
        return (db_get_error(db));
    db_set_cache(db, cache);

    if (!(flags&HAM_IN_MEMORY_DB)) {
        db_header_t *h;
        ham_page_t *page;

        /* create the file */
        st=os_create(filename, flags, mode, &fd);
        if (st) {
            ham_log("os_open of %s failed with status %d (%s)", filename, 
                    st, ham_strerror(st));
            db_set_error(db, st);
            return (st);
        }
        db_set_fd(db, fd);

        /* 
         * allocate a database header page 
         */
        page=db_alloc_page_struct(db);
        if (!page)
            return (db_get_error(db));
        st=db_alloc_page_device(page, PAGE_IGNORE_FREELIST);
        if (st) 
            return (st);
        page_set_type(page, PAGE_TYPE_HEADER);
        db_set_header_page(db, page);
        /* initialize the freelist structure in the header page */
        h=(db_header_t *)page_get_payload(page);
        freel_payload_set_maxsize(&h->_freelist, 
                (db_get_usable_pagesize(db)-sizeof(db_header_t))/
                sizeof(freel_entry_t));
    }

    /* create the backend */
    backend=db_create_backend(db, flags);
    if (!backend) {
        ham_log("unable to create backend with flags 0x%x", flags);
        db_set_error(db, HAM_INV_BACKEND);
        return (HAM_INV_BACKEND);
    }

    /* initialize the backend */
    st=backend->_fun_create(backend, flags);
    if (st) {
        db_set_error(db, st);
        return (st);
    }

    /* store the backend in the database */
    db_set_backend(db, backend);

    /* create the freelist */
    st=freel_create(db);
    if (st) {
        ham_log("unable to create freelist", 0);
        return (st);
    }

    /* set the default key compare functions */
    ham_set_compare_func(db, db_default_compare);
    ham_set_prefix_compare_func(db, 0); /* TODO */
    db_set_dirty(db, HAM_TRUE);

    return (HAM_SUCCESS);
}

ham_status_t
ham_get_error(ham_db_t *db)
{
    return (db_get_error(db));
}

ham_status_t
ham_set_prefix_compare_func(ham_db_t *db, ham_prefix_compare_func_t foo)
{
    db_set_prefix_compare_func(db, foo);
    return (HAM_SUCCESS);
}

ham_status_t
ham_set_compare_func(ham_db_t *db, ham_compare_func_t foo)
{
    db_set_compare_func(db, foo);
    return (HAM_SUCCESS);
}

ham_status_t
ham_find(ham_db_t *db, void *reserved, ham_key_t *key, 
        ham_record_t *record, ham_u32_t flags)
{
    ham_txn_t txn;
    ham_status_t st;
    ham_backend_t *be=db_get_backend(db);

    if (!be)
        return (HAM_INV_BACKEND);
    if (!record)
        return (HAM_INV_PARAMETER);
    if ((st=ham_txn_begin(&txn, db)))
        return (st);

    /*
     * first look up the blob id, then fetch the blob
     */
    st=be->_fun_find(be, &txn, key, record, flags);
    if (st==HAM_SUCCESS) {
        ham_bool_t noblob=HAM_FALSE;

        /*
         * success!
         *
         * sometimes (if the record size is small enough), there's
         * no blob available, but the data is stored in the record's
         * offset.
         */
        if (record->_intflags&KEY_BLOB_SIZE_TINY) {
            /* the highest nibble of the record id is the size of the blob */
            char *p=(char *)&record->_rid;
            record->size=p[sizeof(ham_offset_t)-1];
            noblob=HAM_TRUE;
        }
        else if (record->_intflags&KEY_BLOB_SIZE_SMALL) {
            /* record size is sizeof(ham_offset_t) */
            record->size=sizeof(ham_offset_t);
            noblob=HAM_TRUE;
        }
        else if (record->_intflags&KEY_BLOB_SIZE_EMPTY) {
            /* record size is 0 */
            record->size=0;
            noblob=HAM_TRUE;
        }

        if (noblob && record->size>0) {
            if (!(record->flags & HAM_RECORD_USER_ALLOC)) {
                if (record->size>db_get_record_allocsize(db)) {
                    if (db_get_record_allocdata(db))
                        ham_mem_free(db_get_record_allocdata(db));
                    db_set_record_allocdata(db, ham_mem_alloc(record->size));
                    if (!db_get_record_allocdata(db)) {
                        st=HAM_OUT_OF_MEMORY;
                        db_set_record_allocsize(db, 0);
                    }
                    else {
                        db_set_record_allocsize(db, record->size);
                    }
                }
                record->data=db_get_record_allocdata(db);
            }

            if (!st) 
                memcpy(record->data, &record->_rid, record->size);
        }
        else 
            st=blob_read(db, &txn, record->_rid, record, flags);
    }

    if (st) {
        (void)ham_txn_abort(&txn);
        return (st);
    }

    return (ham_txn_commit(&txn));
}

ham_status_t
ham_insert(ham_db_t *db, void *reserved, ham_key_t *key, 
        ham_record_t *record, ham_u32_t flags)
{
    ham_txn_t txn;
    ham_status_t st;
    ham_backend_t *be=db_get_backend(db);

    if (!be)
        return (HAM_INV_BACKEND);
    if (db_get_flags(db)&HAM_READ_ONLY)
        return (HAM_DB_READ_ONLY);
    if ((db_get_flags(db)&HAM_DISABLE_VAR_KEYLEN) && 
        key->size>db_get_keysize(db))
        return (HAM_INV_KEYSIZE);
    if ((db_get_keysize(db)<=sizeof(ham_offset_t)) && 
        key->size>db_get_keysize(db))
        return (HAM_INV_KEYSIZE);
    if ((st=ham_txn_begin(&txn, db)))
        return (st);

    /*
     * store the index entry; the backend will store the blob
     */
    st=be->_fun_insert(be, &txn, key, record, flags);

    if (st) {
        (void)ham_txn_abort(&txn);
        return (st);
    }

    return (ham_txn_commit(&txn));
}

ham_status_t
ham_erase(ham_db_t *db, void *reserved, ham_key_t *key, ham_u32_t flags)
{
    ham_txn_t txn;
    ham_status_t st;
    ham_u32_t intflags=0;
    ham_offset_t blobid=0;
    ham_backend_t *be=db_get_backend(db);

    if (!be)
        return (HAM_INV_BACKEND);
    if (db_get_flags(db)&HAM_READ_ONLY)
        return (HAM_DB_READ_ONLY);
    if ((st=ham_txn_begin(&txn, db)))
        return (st);

    /*
     * get rid of the index entry, then free the blob
     */
    st=be->_fun_erase(be, &txn, key, &blobid, &intflags, flags);
    if (st==HAM_SUCCESS) {
        if (!((intflags&KEY_BLOB_SIZE_TINY) || 
              (intflags&KEY_BLOB_SIZE_SMALL) ||
              (intflags&KEY_BLOB_SIZE_EMPTY)))
            st=blob_free(db, &txn, blobid, flags); 
    }

    if (st) {
        (void)ham_txn_abort(&txn);
        return (st);
    }

    return (ham_txn_commit(&txn));
}
    
ham_status_t
ham_dump(ham_db_t *db, void *reserved, ham_dump_cb_t cb)
{
    ham_txn_t txn;
    ham_status_t st;
    ham_backend_t *be=db_get_backend(db);

    if (!be)
        return (HAM_INV_BACKEND);
    if (!cb)
        cb=my_dump_cb;
    if ((st=ham_txn_begin(&txn, db)))
        return (st);

    /*
     * call the backend function
     */
    st=be->_fun_dump(be, &txn, cb);

    if (st) {
        (void)ham_txn_abort(&txn);
        return (st);
    }

    return (ham_txn_commit(&txn));
}

ham_status_t
ham_check_integrity(ham_db_t *db, void *reserved)
{
    ham_txn_t txn;
    ham_status_t st;
    ham_backend_t *be=db_get_backend(db);

    /*
     * check the cache integrity
     */
    st=cache_check_integrity(db_get_cache(db));
    if (st)
        return (st);

    if (!be)
        return (HAM_INV_BACKEND);
    if ((st=ham_txn_begin(&txn, db)))
        return (st);

    /*
     * call the backend function
     */
    st=be->_fun_check_integrity(be, &txn);

    if (st) {
        (void)ham_txn_abort(&txn);
        return (st);
    }

    return (ham_txn_commit(&txn));
}

ham_status_t
ham_flush(ham_db_t *db)
{
    return (db_flush_all(db, 0, DB_FLUSH_NODELETE));
}

ham_status_t
ham_close(ham_db_t *db)
{
    ham_status_t st=0;

    /*
     * update the header page, if necessary
     */
    if (db_is_dirty(db)) {
        ham_page_t *page=db_get_header_page(db);

        memcpy(page_get_payload(page), &db_get_header(db), 
                sizeof(db_header_t)-sizeof(freel_payload_t));
        page_set_dirty(page, 1);
    }

    /*
     * flush the freelist
     */
    st=freel_shutdown(db);
    if (st) {
        ham_log("freel_shutdown() failed with status %d (%s)", 
                st, ham_strerror(st));
        return (st);
    }

    /*
     * flush all pages
     */
    st=db_flush_all(db, 0, 0);
    if (st) {
        ham_log("db_flush_all() failed with status %d (%s)", 
                st, ham_strerror(st));
        return (st);
    }

    /* 
     * if we're not in read-only mode, and not an in-memory-database, 
     * and the dirty-flag is true: flush the page-header to disk 
     */
    if (!(db_get_flags(db)&HAM_IN_MEMORY_DB) && 
        db_is_open(db) && 
        (!(db_get_flags(db)&HAM_READ_ONLY)) && 
        db_is_dirty(db)) {

        /* write the database header */
        st=db_write_page_to_device(db_get_header_page(db));
        if (st) {
            ham_log("db_write_page_to_device() failed with status %d (%s)", 
                    st, ham_strerror(st));
            return (st);
        }
    }

    /* close the backend */
    if (db_get_backend(db)) {
        st=db_get_backend(db)->_fun_close(db_get_backend(db));
        if (st) {
            ham_log("backend close() failed with status %d (%s)", 
                    st, ham_strerror(st));
            return (st);
        }
    }

    /* close the file */
    if (!(db_get_flags(db)&HAM_IN_MEMORY_DB) && 
        db_is_open(db)) {
        (void)os_close(db_get_fd(db));
        /* set an invalid database handle */
        db_set_fd(db, -1);
    }

    return (0);
}

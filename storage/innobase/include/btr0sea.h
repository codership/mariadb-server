/*****************************************************************************

Copyright (c) 1996, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, 2020, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/********************************************************************//**
@file include/btr0sea.h
The index tree adaptive search

Created 2/17/1996 Heikki Tuuri
*************************************************************************/

#ifndef btr0sea_h
#define btr0sea_h

#include "dict0dict.h"
#ifdef BTR_CUR_HASH_ADAPT
#include "ha0ha.h"

/** Creates and initializes the adaptive search system at a database start.
@param[in]	hash_size	hash table size. */
void btr_search_sys_create(ulint hash_size);

/** Frees the adaptive search system at a database shutdown. */
void btr_search_sys_free();

/** Disable the adaptive hash search system and empty the index. */
void btr_search_disable();

/** Enable the adaptive hash search system.
@param resize whether buf_pool_resize() is the caller */
void btr_search_enable(bool resize= false);

/*********************************************************************//**
Updates the search info. */
UNIV_INLINE
void
btr_search_info_update(
/*===================*/
	dict_index_t*	index,	/*!< in: index of the cursor */
	btr_cur_t*	cursor);/*!< in: cursor which was just positioned */

/** Tries to guess the right search position based on the hash search info
of the index. Note that if mode is PAGE_CUR_LE, which is used in inserts,
and the function returns TRUE, then cursor->up_match and cursor->low_match
both have sensible values.
@param[in,out]	index		index
@param[in,out]	info		index search info
@param[in]	tuple		logical record
@param[in]	mode		PAGE_CUR_L, ....
@param[in]	latch_mode	BTR_SEARCH_LEAF, ...;
				NOTE that only if has_search_latch is 0, we will
				have a latch set on the cursor page, otherwise
				we assume the caller uses his search latch
				to protect the record!
@param[out]	cursor		tree cursor
@param[in]	ahi_latch	the adaptive hash index latch being held,
				or NULL
@param[in]	mtr		mini transaction
@return whether the search succeeded */
bool
btr_search_guess_on_hash(
	dict_index_t*	index,
	btr_search_t*	info,
	const dtuple_t*	tuple,
	ulint		mode,
	ulint		latch_mode,
	btr_cur_t*	cursor,
	mtr_t*		mtr);

/** Move or delete hash entries for moved records, usually in a page split.
If new_block is already hashed, then any hash index for block is dropped.
If new_block is not hashed, and block is hashed, then a new hash index is
built to new_block with the same parameters as block.
@param[in,out]	new_block	destination page
@param[in,out]	block		source page (subject to deletion later) */
void
btr_search_move_or_delete_hash_entries(
	buf_block_t*	new_block,
	buf_block_t*	block);

/** Drop any adaptive hash index entries that point to an index page.
@param[in,out]	block	block containing index page, s- or x-latched, or an
			index page for which we know that
			block->buf_fix_count == 0 or it is an index page which
			has already been removed from the buf_pool->page_hash
			i.e.: it is in state BUF_BLOCK_REMOVE_HASH
@param[in]	garbage_collect	drop ahi only if the index is marked
				as freed */
void btr_search_drop_page_hash_index(buf_block_t* block,
				     bool garbage_collect);

/** Drop possible adaptive hash index entries when a page is evicted
from the buffer pool or freed in a file, or the index is being dropped.
@param[in]	page_id		page id */
void btr_search_drop_page_hash_when_freed(const page_id_t page_id);

/** Updates the page hash index when a single record is inserted on a page.
@param[in]	cursor	cursor which was positioned to the place to insert
			using btr_cur_search_, and the new record has been
			inserted next to the cursor.
@param[in]	ahi_latch	the adaptive hash index latch */
void
btr_search_update_hash_node_on_insert(btr_cur_t* cursor, rw_lock_t* ahi_latch);

/** Updates the page hash index when a single record is inserted on a page.
@param[in,out]	cursor		cursor which was positioned to the
				place to insert using btr_cur_search_...,
				and the new record has been inserted next
				to the cursor
@param[in]	ahi_latch	the adaptive hash index latch */
void
btr_search_update_hash_on_insert(btr_cur_t* cursor, rw_lock_t* ahi_latch);

/** Updates the page hash index when a single record is deleted from a page.
@param[in]	cursor	cursor which was positioned on the record to delete
			using btr_cur_search_, the record is not yet deleted.*/
void btr_search_update_hash_on_delete(btr_cur_t* cursor);

/** Validates the search system.
@return true if ok */
bool btr_search_validate();

/** Lock all search latches in exclusive mode. */
static inline void btr_search_x_lock_all();

/** Unlock all search latches from exclusive mode. */
static inline void btr_search_x_unlock_all();

/** Lock all search latches in shared mode. */
static inline void btr_search_s_lock_all();

#ifdef UNIV_DEBUG
/** Check if thread owns all the search latches.
@param[in]	mode	lock mode check
@retval true if owns all of them
@retval false if does not own some of them */
static inline bool btr_search_own_all(ulint mode);

/** Check if thread owns any of the search latches.
@param[in]	mode	lock mode check
@retval true if owns any of them
@retval false if owns no search latch */
static inline bool btr_search_own_any(ulint mode);

/** @return whether this thread holds any of the search latches */
static inline bool btr_search_own_any();
#endif /* UNIV_DEBUG */

/** Unlock all search latches from shared mode. */
static inline void btr_search_s_unlock_all();

/** Get the latch based on index attributes.
A latch is selected from an array of latches using pair of index-id, space-id.
@param[in]	index	index handler
@return latch */
static inline rw_lock_t* btr_get_search_latch(const dict_index_t* index);

/** Get the hash-table based on index attributes.
A table is selected from an array of tables using pair of index-id, space-id.
@param[in]	index	index handler
@return hash table */
static inline hash_table_t* btr_get_search_table(const dict_index_t* index);

#ifdef UNIV_DEBUG
/** @return if the index is marked as freed */
bool btr_search_check_marked_free_index(const buf_block_t *block);
#endif /* UNIV_DEBUG */

#else /* BTR_CUR_HASH_ADAPT */
# define btr_search_sys_create(size)
# define btr_search_sys_free()
# define btr_search_drop_page_hash_index(block, garbage_collect)
# define btr_search_s_lock_all(index)
# define btr_search_s_unlock_all(index)
# define btr_search_info_update(index, cursor)
# define btr_search_move_or_delete_hash_entries(new_block, block)
# define btr_search_update_hash_on_insert(cursor, ahi_latch)
# define btr_search_update_hash_on_delete(cursor)
#ifdef UNIV_DEBUG
# define btr_search_check_marked_free_index(block)
#endif /* UNIV_DEBUG */
#endif /* BTR_CUR_HASH_ADAPT */

#ifdef BTR_CUR_ADAPT
/** Create and initialize search info.
@param[in,out]	heap		heap where created
@return own: search info struct */
static inline btr_search_t* btr_search_info_create(mem_heap_t* heap)
	MY_ATTRIBUTE((nonnull, warn_unused_result));

/** @return the search info of an index */
static inline btr_search_t* btr_search_get_info(dict_index_t* index)
{
	return(index->search_info);
}
#endif /* BTR_CUR_ADAPT */

/** The search info struct in an index */
struct btr_search_t{
	/* @{ The following fields are not protected by any latch.
	Unfortunately, this means that they must be aligned to
	the machine word, i.e., they cannot be turned into bit-fields. */
	buf_block_t* root_guess;/*!< the root page frame when it was last time
				fetched, or NULL */
#ifdef BTR_CUR_HASH_ADAPT
	ulint	hash_analysis;	/*!< when this exceeds
				BTR_SEARCH_HASH_ANALYSIS, the hash
				analysis starts; this is reset if no
				success noticed */
	ibool	last_hash_succ;	/*!< TRUE if the last search would have
				succeeded, or did succeed, using the hash
				index; NOTE that the value here is not exact:
				it is not calculated for every search, and the
				calculation itself is not always accurate! */
	ulint	n_hash_potential;
				/*!< number of consecutive searches
				which would have succeeded, or did succeed,
				using the hash index;
				the range is 0 .. BTR_SEARCH_BUILD_LIMIT + 5 */
	/* @} */
	ulint	ref_count;	/*!< Number of blocks in this index tree
				that have search index built
				i.e. block->index points to this index.
				Protected by search latch except
				when during initialization in
				btr_search_info_create(). */

	/*---------------------- @{ */
	ulint	n_fields;	/*!< recommended prefix length for hash search:
				number of full fields */
	ulint	n_bytes;	/*!< recommended prefix: number of bytes in
				an incomplete field
				@see BTR_PAGE_MAX_REC_SIZE */
	bool	left_side;	/*!< true or false, depending on whether
				the leftmost record of several records with
				the same prefix should be indexed in the
				hash index */
	/*---------------------- @} */
#ifdef UNIV_SEARCH_PERF_STAT
	ulint	n_hash_succ;	/*!< number of successful hash searches thus
				far */
	ulint	n_hash_fail;	/*!< number of failed hash searches */
	ulint	n_patt_succ;	/*!< number of successful pattern searches thus
				far */
	ulint	n_searches;	/*!< number of searches */
#endif /* UNIV_SEARCH_PERF_STAT */
#endif /* BTR_CUR_HASH_ADAPT */
#ifdef UNIV_DEBUG
	ulint	magic_n;	/*!< magic number @see BTR_SEARCH_MAGIC_N */
/** value of btr_search_t::magic_n, used in assertions */
# define BTR_SEARCH_MAGIC_N	1112765
#endif /* UNIV_DEBUG */
};

#ifdef BTR_CUR_HASH_ADAPT
/** @return number of leaf pages pointed to by the adaptive hash index */
inline ulint dict_index_t::n_ahi_pages() const
{
  if (!btr_search_enabled)
    return 0;
  rw_lock_t *latch = btr_get_search_latch(this);
  rw_lock_s_lock(latch);
  ulint ref_count= search_info->ref_count;
  rw_lock_s_unlock(latch);
  return ref_count;
}

/** The hash index system */
struct btr_search_sys_t{
	hash_table_t**	hash_tables;	/*!< the adaptive hash tables,
					mapping dtuple_fold values
					to rec_t pointers on index pages */
};

/** Latches protecting access to adaptive hash index. */
extern rw_lock_t**		btr_search_latches;

/** The adaptive hash index */
extern btr_search_sys_t*	btr_search_sys;

#ifdef UNIV_SEARCH_PERF_STAT
/** Number of successful adaptive hash index lookups */
extern ulint	btr_search_n_succ;
/** Number of failed adaptive hash index lookups */
extern ulint	btr_search_n_hash_fail;
#endif /* UNIV_SEARCH_PERF_STAT */

/** After change in n_fields or n_bytes in info, this many rounds are waited
before starting the hash analysis again: this is to save CPU time when there
is no hope in building a hash index. */
#define BTR_SEARCH_HASH_ANALYSIS	17

/** Limit of consecutive searches for trying a search shortcut on the search
pattern */
#define BTR_SEARCH_ON_PATTERN_LIMIT	3

/** Limit of consecutive searches for trying a search shortcut using
the hash index */
#define BTR_SEARCH_ON_HASH_LIMIT	3

/** We do this many searches before trying to keep the search latch
over calls from MySQL. If we notice someone waiting for the latch, we
again set this much timeout. This is to reduce contention. */
#define BTR_SEA_TIMEOUT			10000
#endif /* BTR_CUR_HASH_ADAPT */

#include "btr0sea.inl"

#endif

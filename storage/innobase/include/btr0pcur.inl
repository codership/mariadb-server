/*****************************************************************************

Copyright (c) 1996, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2015, 2020, MariaDB Corporation.

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

/**************************************************//**
@file include/btr0pcur.ic
The index tree persistent cursor

Created 2/23/1996 Heikki Tuuri
*******************************************************/


/*********************************************************//**
Gets the rel_pos field for a cursor whose position has been stored.
@return BTR_PCUR_ON, ... */
UNIV_INLINE
ulint
btr_pcur_get_rel_pos(
/*=================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor);
	ut_ad(cursor->old_rec);
	ut_ad(cursor->old_stored);
	ut_ad(cursor->pos_state == BTR_PCUR_WAS_POSITIONED
	      || cursor->pos_state == BTR_PCUR_IS_POSITIONED);

	return(cursor->rel_pos);
}

#ifdef UNIV_DEBUG
/*********************************************************//**
Returns the btr cursor component of a persistent cursor.
@return pointer to btr cursor component */
UNIV_INLINE
btr_cur_t*
btr_pcur_get_btr_cur(
/*=================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	const btr_cur_t*	btr_cur = &cursor->btr_cur;
	return((btr_cur_t*) btr_cur);
}

/*********************************************************//**
Returns the page cursor component of a persistent cursor.
@return pointer to page cursor component */
UNIV_INLINE
page_cur_t*
btr_pcur_get_page_cur(
/*==================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	return(btr_cur_get_page_cur(btr_pcur_get_btr_cur(cursor)));
}

/*********************************************************//**
Returns the page of a persistent cursor.
@return pointer to the page */
UNIV_INLINE
page_t*
btr_pcur_get_page(
/*==============*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);

	return(btr_cur_get_page(btr_pcur_get_btr_cur(cursor)));
}

/*********************************************************//**
Returns the buffer block of a persistent cursor.
@return pointer to the block */
UNIV_INLINE
buf_block_t*
btr_pcur_get_block(
/*===============*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);

	return(btr_cur_get_block(btr_pcur_get_btr_cur(cursor)));
}

/*********************************************************//**
Returns the record of a persistent cursor.
@return pointer to the record */
UNIV_INLINE
rec_t*
btr_pcur_get_rec(
/*=============*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	return(btr_cur_get_rec(btr_pcur_get_btr_cur(cursor)));
}
#endif /* UNIV_DEBUG */

/**************************************************************//**
Gets the up_match value for a pcur after a search.
@return number of matched fields at the cursor or to the right if
search mode was PAGE_CUR_GE, otherwise undefined */
UNIV_INLINE
ulint
btr_pcur_get_up_match(
/*==================*/
	const btr_pcur_t*	cursor) /*!< in: persistent cursor */
{
	const btr_cur_t*	btr_cursor;

	ut_ad((cursor->pos_state == BTR_PCUR_WAS_POSITIONED)
	      || (cursor->pos_state == BTR_PCUR_IS_POSITIONED));

	btr_cursor = btr_pcur_get_btr_cur(cursor);

	ut_ad(btr_cursor->up_match != ULINT_UNDEFINED);

	return(btr_cursor->up_match);
}

/**************************************************************//**
Gets the low_match value for a pcur after a search.
@return number of matched fields at the cursor or to the right if
search mode was PAGE_CUR_LE, otherwise undefined */
UNIV_INLINE
ulint
btr_pcur_get_low_match(
/*===================*/
	const btr_pcur_t*	cursor) /*!< in: persistent cursor */
{
	const btr_cur_t*	btr_cursor;

	ut_ad((cursor->pos_state == BTR_PCUR_WAS_POSITIONED)
	      || (cursor->pos_state == BTR_PCUR_IS_POSITIONED));

	btr_cursor = btr_pcur_get_btr_cur(cursor);
	ut_ad(btr_cursor->low_match != ULINT_UNDEFINED);

	return(btr_cursor->low_match);
}

/*********************************************************//**
Checks if the persistent cursor is after the last user record on
a page. */
UNIV_INLINE
ibool
btr_pcur_is_after_last_on_page(
/*===========================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	return(page_cur_is_after_last(btr_pcur_get_page_cur(cursor)));
}

/*********************************************************//**
Checks if the persistent cursor is before the first user record on
a page. */
UNIV_INLINE
ibool
btr_pcur_is_before_first_on_page(
/*=============================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	return(page_cur_is_before_first(btr_pcur_get_page_cur(cursor)));
}

/*********************************************************//**
Checks if the persistent cursor is on a user record. */
UNIV_INLINE
ibool
btr_pcur_is_on_user_rec(
/*====================*/
	const btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	if (btr_pcur_is_before_first_on_page(cursor)
	    || btr_pcur_is_after_last_on_page(cursor)) {

		return(FALSE);
	}

	return(TRUE);
}

/*********************************************************//**
Checks if the persistent cursor is before the first user record in
the index tree. */
static inline bool btr_pcur_is_before_first_in_tree(btr_pcur_t* cursor)
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	return !page_has_prev(btr_pcur_get_page(cursor))
		&& page_cur_is_before_first(btr_pcur_get_page_cur(cursor));
}

/*********************************************************//**
Checks if the persistent cursor is after the last user record in
the index tree. */
static inline bool btr_pcur_is_after_last_in_tree(btr_pcur_t* cursor)
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	return !page_has_next(btr_pcur_get_page(cursor))
		&& page_cur_is_after_last(btr_pcur_get_page_cur(cursor));
}

/*********************************************************//**
Moves the persistent cursor to the next record on the same page. */
UNIV_INLINE
void
btr_pcur_move_to_next_on_page(
/*==========================*/
	btr_pcur_t*	cursor)	/*!< in/out: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	page_cur_move_to_next(btr_pcur_get_page_cur(cursor));

	cursor->old_stored = false;
}

/*********************************************************//**
Moves the persistent cursor to the previous record on the same page. */
UNIV_INLINE
void
btr_pcur_move_to_prev_on_page(
/*==========================*/
	btr_pcur_t*	cursor)	/*!< in/out: persistent cursor */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	page_cur_move_to_prev(btr_pcur_get_page_cur(cursor));

	cursor->old_stored = false;
}

/*********************************************************//**
Moves the persistent cursor to the next user record in the tree. If no user
records are left, the cursor ends up 'after last in tree'.
@return TRUE if the cursor moved forward, ending on a user record */
UNIV_INLINE
ibool
btr_pcur_move_to_next_user_rec(
/*===========================*/
	btr_pcur_t*	cursor,	/*!< in: persistent cursor; NOTE that the
				function may release the page latch */
	mtr_t*		mtr)	/*!< in: mtr */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);
	cursor->old_stored = false;
loop:
	if (btr_pcur_is_after_last_on_page(cursor)) {
		if (btr_pcur_is_after_last_in_tree(cursor)) {
			return(FALSE);
		}

		btr_pcur_move_to_next_page(cursor, mtr);
	} else {
		btr_pcur_move_to_next_on_page(cursor);
	}

	if (btr_pcur_is_on_user_rec(cursor)) {

		return(TRUE);
	}

	goto loop;
}

/*********************************************************//**
Moves the persistent cursor to the next record in the tree. If no records are
left, the cursor stays 'after last in tree'.
@return TRUE if the cursor was not after last in tree */
UNIV_INLINE
ibool
btr_pcur_move_to_next(
/*==================*/
	btr_pcur_t*	cursor,	/*!< in: persistent cursor; NOTE that the
				function may release the page latch */
	mtr_t*		mtr)	/*!< in: mtr */
{
	ut_ad(cursor->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	cursor->old_stored = false;

	if (btr_pcur_is_after_last_on_page(cursor)) {
		if (btr_pcur_is_after_last_in_tree(cursor)) {
			return(FALSE);
		}

		btr_pcur_move_to_next_page(cursor, mtr);
		return(TRUE);
	}

	btr_pcur_move_to_next_on_page(cursor);
	return(TRUE);
}

/**************************************************************//**
Commits the mtr and sets the pcur latch mode to BTR_NO_LATCHES,
that is, the cursor becomes detached.
Function btr_pcur_store_position should be used before calling this,
if restoration of cursor is wanted later. */
UNIV_INLINE
void
btr_pcur_commit_specify_mtr(
/*========================*/
	btr_pcur_t*	pcur,	/*!< in: persistent cursor */
	mtr_t*		mtr)	/*!< in: mtr to commit */
{
	ut_ad(pcur->pos_state == BTR_PCUR_IS_POSITIONED);

	pcur->latch_mode = BTR_NO_LATCHES;

	mtr_commit(mtr);

	pcur->pos_state = BTR_PCUR_WAS_POSITIONED;
}

/** Commits the mtr and sets the clustered index pcur and secondary index
pcur latch mode to BTR_NO_LATCHES, that is, the cursor becomes detached.
Function btr_pcur_store_position should be used for both cursor before
calling this, if restoration of cursor is wanted later.
@param[in]	pcur		persistent cursor
@param[in]	sec_pcur	secondary index persistent cursor
@param[in]	mtr		mtr to commit */
UNIV_INLINE
void
btr_pcurs_commit_specify_mtr(
	btr_pcur_t*	pcur,
	btr_pcur_t*	sec_pcur,
	mtr_t*		mtr)
{
	ut_ad(pcur->pos_state == BTR_PCUR_IS_POSITIONED);
	ut_ad(sec_pcur->pos_state == BTR_PCUR_IS_POSITIONED);

	pcur->latch_mode = BTR_NO_LATCHES;
	sec_pcur->latch_mode = BTR_NO_LATCHES;

	mtr_commit(mtr);

	pcur->pos_state = BTR_PCUR_WAS_POSITIONED;
	sec_pcur->pos_state = BTR_PCUR_WAS_POSITIONED;
}

/**************************************************************//**
Sets the old_rec_buf field to NULL. */
UNIV_INLINE
void
btr_pcur_init(
/*==========*/
	btr_pcur_t*	pcur)	/*!< in: persistent cursor */
{
	pcur->old_stored = false;
	pcur->old_rec_buf = NULL;
	pcur->old_rec = NULL;

	pcur->btr_cur.rtr_info = NULL;
}

/** Free old_rec_buf.
@param[in]	pcur	Persistent cursor holding old_rec to be freed. */
UNIV_INLINE
void
btr_pcur_free(
	btr_pcur_t*	pcur)
{
	ut_free(pcur->old_rec_buf);
}

/**************************************************************//**
Initializes and opens a persistent cursor to an index tree. It should be
closed with btr_pcur_close. */
UNIV_INLINE
dberr_t
btr_pcur_open_low(
/*==============*/
	dict_index_t*	index,	/*!< in: index */
	ulint		level,	/*!< in: level in the btree */
	const dtuple_t*	tuple,	/*!< in: tuple on which search done */
	page_cur_mode_t	mode,	/*!< in: PAGE_CUR_L, ...;
				NOTE that if the search is made using a unique
				prefix of a record, mode should be
				PAGE_CUR_LE, not PAGE_CUR_GE, as the latter
				may end up on the previous page from the
				record! */
	ulint		latch_mode,/*!< in: BTR_SEARCH_LEAF, ... */
	btr_pcur_t*	cursor, /*!< in: memory buffer for persistent cursor */
	const char*	file,	/*!< in: file name */
	unsigned	line,	/*!< in: line where called */
	ib_uint64_t	autoinc,/*!< in: PAGE_ROOT_AUTO_INC to be written
				(0 if none) */
	mtr_t*		mtr)	/*!< in: mtr */
{
	btr_cur_t*	btr_cursor;
	dberr_t err = DB_SUCCESS;

	/* Initialize the cursor */

	btr_pcur_init(cursor);

	cursor->latch_mode = BTR_LATCH_MODE_WITHOUT_FLAGS(latch_mode);
	cursor->search_mode = mode;

	/* Search with the tree cursor */

	btr_cursor = btr_pcur_get_btr_cur(cursor);

	ut_ad(!dict_index_is_spatial(index));

	err = btr_cur_search_to_nth_level(
		index, level, tuple, mode, latch_mode, btr_cursor,
		file, line, mtr, autoinc);

	if (UNIV_UNLIKELY(err != DB_SUCCESS)) {
		ib::warn() << "btr_pcur_open_low"
			   << " level: " << level
			   << " called from file: "
			   << file << " line: " << line
			   << " table: " << index->table->name
			   << " index: " << index->name
			   << " error: " << err;
	}

	cursor->pos_state = BTR_PCUR_IS_POSITIONED;

	cursor->trx_if_known = NULL;

	return(err);
}

/** Opens an persistent cursor to an index tree without initializing the
cursor.
@param index      index
@param tuple      tuple on which search done
@param mode       PAGE_CUR_L, ...; NOTE that if the search is made using a
                  unique prefix of a record, mode should be PAGE_CUR_LE, not
                  PAGE_CUR_GE, as the latter may end up on the previous page of
                  the record!
@param latch_mode BTR_SEARCH_LEAF, ...; NOTE that if ahi_latch then we might
                  not acquire a cursor page latch, but assume that the
                  ahi_latch protects the record!
@param cursor     memory buffer for persistent cursor
@param file       file name
@param line       line where called
@param mtr        mtr
@return DB_SUCCESS on success or error code otherwise. */
UNIV_INLINE
dberr_t btr_pcur_open_with_no_init_func(dict_index_t *index,
                                        const dtuple_t *tuple,
                                        page_cur_mode_t mode, ulint latch_mode,
                                        btr_pcur_t *cursor, const char *file,
                                        unsigned line, mtr_t *mtr)
{
	btr_cur_t*	btr_cursor;
	dberr_t		err = DB_SUCCESS;

	cursor->latch_mode = BTR_LATCH_MODE_WITHOUT_INTENTION(latch_mode);
	cursor->search_mode = mode;

	/* Search with the tree cursor */

	btr_cursor = btr_pcur_get_btr_cur(cursor);

	err = btr_cur_search_to_nth_level(
		index, 0, tuple, mode, latch_mode, btr_cursor,
		file, line, mtr);

	cursor->pos_state = BTR_PCUR_IS_POSITIONED;

	cursor->old_stored = false;

	cursor->trx_if_known = NULL;
	return err;
}

/*****************************************************************//**
Opens a persistent cursor at either end of an index. */
UNIV_INLINE
dberr_t
btr_pcur_open_at_index_side(
/*========================*/
	bool		from_left,	/*!< in: true if open to the low end,
					false if to the high end */
	dict_index_t*	index,		/*!< in: index */
	ulint		latch_mode,	/*!< in: latch mode */
	btr_pcur_t*	pcur,		/*!< in/out: cursor */
	bool		init_pcur,	/*!< in: whether to initialize pcur */
	ulint		level,		/*!< in: level to search for
					(0=leaf) */
	mtr_t*		mtr)		/*!< in/out: mini-transaction */
{
	dberr_t		err = DB_SUCCESS;

	pcur->latch_mode = BTR_LATCH_MODE_WITHOUT_FLAGS(latch_mode);

	pcur->search_mode = from_left ? PAGE_CUR_G : PAGE_CUR_L;

	if (init_pcur) {
		btr_pcur_init(pcur);
	}

	err = btr_cur_open_at_index_side(
		from_left, index, latch_mode,
		btr_pcur_get_btr_cur(pcur), level, mtr);
	pcur->pos_state = BTR_PCUR_IS_POSITIONED;

	pcur->old_stored = false;

	pcur->trx_if_known = NULL;

	return (err);
}

/**********************************************************************//**
Positions a cursor at a randomly chosen position within a B-tree.
@return true if the index is available and we have put the cursor, false
if the index is unavailable */
UNIV_INLINE
bool
btr_pcur_open_at_rnd_pos_func(
/*==========================*/
	dict_index_t*	index,		/*!< in: index */
	ulint		latch_mode,	/*!< in: BTR_SEARCH_LEAF, ... */
	btr_pcur_t*	cursor,		/*!< in/out: B-tree pcur */
	const char*	file,		/*!< in: file name */
	unsigned	line,		/*!< in: line where called */
	mtr_t*		mtr)		/*!< in: mtr */
{
	/* Initialize the cursor */

	cursor->latch_mode = latch_mode;
	cursor->search_mode = PAGE_CUR_G;

	btr_pcur_init(cursor);

	bool	available;

	available = btr_cur_open_at_rnd_pos_func(index, latch_mode,
						 btr_pcur_get_btr_cur(cursor),
						 file, line, mtr);
	cursor->pos_state = BTR_PCUR_IS_POSITIONED;
	cursor->old_stored = false;

	cursor->trx_if_known = NULL;

	return(available);
}

/**************************************************************//**
Frees the possible memory heap of a persistent cursor and sets the latch
mode of the persistent cursor to BTR_NO_LATCHES.
WARNING: this function does not release the latch on the page where the
cursor is currently positioned. The latch is acquired by the
"move to next/previous" family of functions. Since recursive shared locks
are not allowed, you must take care (if using the cursor in S-mode) to
manually release the latch by either calling
btr_leaf_page_release(btr_pcur_get_block(&pcur), pcur.latch_mode, mtr)
or by committing the mini-transaction right after btr_pcur_close().
A subsequent attempt to crawl the same page in the same mtr would cause
an assertion failure. */
UNIV_INLINE
void
btr_pcur_close(
/*===========*/
	btr_pcur_t*	cursor)	/*!< in: persistent cursor */
{
	ut_free(cursor->old_rec_buf);

	if (cursor->btr_cur.rtr_info) {
		rtr_clean_rtr_info(cursor->btr_cur.rtr_info, true);
		cursor->btr_cur.rtr_info = NULL;
	}

	cursor->old_rec = NULL;
	cursor->old_rec_buf = NULL;
	cursor->btr_cur.page_cur.rec = NULL;
	cursor->btr_cur.page_cur.block = NULL;

	cursor->old_rec = NULL;
	cursor->old_stored = false;

	cursor->latch_mode = BTR_NO_LATCHES;
	cursor->pos_state = BTR_PCUR_NOT_POSITIONED;

	cursor->trx_if_known = NULL;
}

/*********************************************************//**
Moves the persistent cursor to the infimum record on the same page. */
UNIV_INLINE
void
btr_pcur_move_before_first_on_page(
/*===============================*/
	btr_pcur_t*	cursor) /*!< in/out: persistent cursor */
{
	ut_ad(cursor->latch_mode != BTR_NO_LATCHES);

	page_cur_set_before_first(btr_pcur_get_block(cursor),
		btr_pcur_get_page_cur(cursor));

	cursor->old_stored = false;
}

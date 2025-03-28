/*****************************************************************************

Copyright (c) 1994, 2019, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2016, 2021, MariaDB Corporation.

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
@file include/page0page.ic
Index page routines

Created 2/2/1994 Heikki Tuuri
*******************************************************/

#ifndef page0page_ic
#define page0page_ic

#ifndef UNIV_INNOCHECKSUM
#include "mach0data.h"
#include "rem0cmp.h"
#include "mtr0log.h"
#include "page0zip.h"

/*************************************************************//**
Returns the max trx id field value. */
UNIV_INLINE
trx_id_t
page_get_max_trx_id(
/*================*/
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page);

	return(mach_read_from_8(page + PAGE_HEADER + PAGE_MAX_TRX_ID));
}

/*************************************************************//**
Sets the max trx id field value if trx_id is bigger than the previous
value. */
UNIV_INLINE
void
page_update_max_trx_id(
/*===================*/
	buf_block_t*	block,	/*!< in/out: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	trx_id_t	trx_id,	/*!< in: transaction id */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	ut_ad(block);
	ut_ad(mtr_memo_contains(mtr, block, MTR_MEMO_PAGE_X_FIX));
	/* During crash recovery, this function may be called on
	something else than a leaf page of a secondary index or the
	insert buffer index tree (dict_index_is_sec_or_ibuf() returns
	TRUE for the dummy indexes constructed during redo log
	application).  In that case, PAGE_MAX_TRX_ID is unused,
	and trx_id is usually zero. */
	ut_ad(trx_id || recv_recovery_is_on());
	ut_ad(page_is_leaf(buf_block_get_frame(block)));

	if (page_get_max_trx_id(buf_block_get_frame(block)) < trx_id) {

		page_set_max_trx_id(block, page_zip, trx_id, mtr);
	}
}

/** Read the AUTO_INCREMENT value from a clustered index root page.
@param[in]	page	clustered index root page
@return	the persisted AUTO_INCREMENT value */
UNIV_INLINE
ib_uint64_t
page_get_autoinc(const page_t* page)
{
	ut_ad(fil_page_index_page_check(page));
	ut_ad(!page_has_siblings(page));
	return(mach_read_from_8(PAGE_HEADER + PAGE_ROOT_AUTO_INC + page));
}

/*************************************************************//**
Returns the RTREE SPLIT SEQUENCE NUMBER (FIL_RTREE_SPLIT_SEQ_NUM).
@return	SPLIT SEQUENCE NUMBER */
UNIV_INLINE
node_seq_t
page_get_ssn_id(
/*============*/
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page);

	return(static_cast<node_seq_t>(
		mach_read_from_8(page + FIL_RTREE_SPLIT_SEQ_NUM)));
}

/*************************************************************//**
Sets the RTREE SPLIT SEQUENCE NUMBER field value */
UNIV_INLINE
void
page_set_ssn_id(
/*============*/
	buf_block_t*	block,	/*!< in/out: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	node_seq_t	ssn_id,	/*!< in: transaction id */
	mtr_t*		mtr)	/*!< in/out: mini-transaction */
{
	page_t*	page = buf_block_get_frame(block);

	ut_ad(!mtr || mtr_memo_contains_flagged(mtr, block,
						MTR_MEMO_PAGE_SX_FIX
						| MTR_MEMO_PAGE_X_FIX));

	if (page_zip) {
		mach_write_to_8(page + FIL_RTREE_SPLIT_SEQ_NUM, ssn_id);
		page_zip_write_header(page_zip,
				      page + FIL_RTREE_SPLIT_SEQ_NUM,
				      8, mtr);
	} else if (mtr) {
		mlog_write_ull(page + FIL_RTREE_SPLIT_SEQ_NUM, ssn_id, mtr);
	} else {
		mach_write_to_8(page + FIL_RTREE_SPLIT_SEQ_NUM, ssn_id);
	}
}

#endif /* !UNIV_INNOCHECKSUM */

/*************************************************************//**
Reads the given header field. */
UNIV_INLINE
uint16_t
page_header_get_field(
/*==================*/
	const page_t*	page,	/*!< in: page */
	ulint		field)	/*!< in: PAGE_LEVEL, ... */
{
	ut_ad(page);
	ut_ad(field <= PAGE_INDEX_ID);

	return(mach_read_from_2(page + PAGE_HEADER + field));
}

#ifndef UNIV_INNOCHECKSUM
/*************************************************************//**
Sets the given header field. */
UNIV_INLINE
void
page_header_set_field(
/*==================*/
	page_t*		page,	/*!< in/out: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	ulint		field,	/*!< in: PAGE_N_DIR_SLOTS, ... */
	ulint		val)	/*!< in: value */
{
	ut_ad(page);
	ut_ad(field <= PAGE_N_RECS);
	ut_ad(field == PAGE_N_HEAP || val < srv_page_size);
	ut_ad(field != PAGE_N_HEAP || (val & 0x7fff) < srv_page_size);

	mach_write_to_2(page + PAGE_HEADER + field, val);
	if (page_zip) {
		page_zip_write_header(page_zip,
				      page + PAGE_HEADER + field, 2, NULL);
	}
}

/*************************************************************//**
Returns the offset stored in the given header field.
@return offset from the start of the page, or 0 */
UNIV_INLINE
uint16_t
page_header_get_offs(
/*=================*/
	const page_t*	page,	/*!< in: page */
	ulint		field)	/*!< in: PAGE_FREE, ... */
{
	ut_ad((field == PAGE_FREE)
	      || (field == PAGE_LAST_INSERT)
	      || (field == PAGE_HEAP_TOP));

	uint16_t offs = page_header_get_field(page, field);

	ut_ad((field != PAGE_HEAP_TOP) || offs);

	return(offs);
}

/*************************************************************//**
Sets the pointer stored in the given header field. */
UNIV_INLINE
void
page_header_set_ptr(
/*================*/
	page_t*		page,	/*!< in: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	ulint		field,	/*!< in: PAGE_FREE, ... */
	const byte*	ptr)	/*!< in: pointer or NULL*/
{
	ulint	offs;

	ut_ad(page);
	ut_ad((field == PAGE_FREE)
	      || (field == PAGE_LAST_INSERT)
	      || (field == PAGE_HEAP_TOP));

	if (ptr == NULL) {
		offs = 0;
	} else {
		offs = ulint(ptr - page);
	}

	ut_ad((field != PAGE_HEAP_TOP) || offs);

	page_header_set_field(page, page_zip, field, offs);
}

/*************************************************************//**
Resets the last insert info field in the page header. Writes to mlog
about this operation. */
UNIV_INLINE
void
page_header_reset_last_insert(
/*==========================*/
	page_t*		page,	/*!< in/out: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	mtr_t*		mtr)	/*!< in: mtr */
{
	ut_ad(page != NULL);
	ut_ad(mtr != NULL);

	if (page_zip) {
		mach_write_to_2(page + (PAGE_HEADER + PAGE_LAST_INSERT), 0);
		page_zip_write_header(page_zip,
				      page + (PAGE_HEADER + PAGE_LAST_INSERT),
				      2, mtr);
	} else {
		mlog_write_ulint(page + (PAGE_HEADER + PAGE_LAST_INSERT), 0,
				 MLOG_2BYTES, mtr);
	}
}

/***************************************************************//**
Returns the heap number of a record.
@return heap number */
UNIV_INLINE
ulint
page_rec_get_heap_no(
/*=================*/
	const rec_t*	rec)	/*!< in: the physical record */
{
	if (page_rec_is_comp(rec)) {
		return(rec_get_heap_no_new(rec));
	} else {
		return(rec_get_heap_no_old(rec));
	}
}

/** Determine whether an index page record is a user record.
@param[in]	rec	record in an index page
@return true if a user record */
inline
bool
page_rec_is_user_rec(const rec_t* rec)
{
	ut_ad(page_rec_check(rec));
	return(page_rec_is_user_rec_low(page_offset(rec)));
}

/** Determine whether an index page record is the supremum record.
@param[in]	rec	record in an index page
@return true if the supremum record */
inline
bool
page_rec_is_supremum(const rec_t* rec)
{
	ut_ad(page_rec_check(rec));
	return(page_rec_is_supremum_low(page_offset(rec)));
}

/** Determine whether an index page record is the infimum record.
@param[in]	rec	record in an index page
@return true if the infimum record */
inline
bool
page_rec_is_infimum(const rec_t* rec)
{
	ut_ad(page_rec_check(rec));
	return(page_rec_is_infimum_low(page_offset(rec)));
}

/************************************************************//**
true if the record is the first user record on a page.
@return true if the first user record */
UNIV_INLINE
bool
page_rec_is_first(
/*==============*/
	const rec_t*	rec,	/*!< in: record */
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page_get_n_recs(page) > 0);

	return(page_rec_get_next_const(page_get_infimum_rec(page)) == rec);
}

/************************************************************//**
true if the record is the second user record on a page.
@return true if the second user record */
UNIV_INLINE
bool
page_rec_is_second(
/*===============*/
	const rec_t*	rec,	/*!< in: record */
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page_get_n_recs(page) > 1);

	return(page_rec_get_next_const(
		page_rec_get_next_const(page_get_infimum_rec(page))) == rec);
}

/************************************************************//**
true if the record is the last user record on a page.
@return true if the last user record */
UNIV_INLINE
bool
page_rec_is_last(
/*=============*/
	const rec_t*	rec,	/*!< in: record */
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page_get_n_recs(page) > 0);

	return(page_rec_get_next_const(rec) == page_get_supremum_rec(page));
}

/************************************************************//**
true if distance between the records (measured in number of times we have to
move to the next record) is at most the specified value */
UNIV_INLINE
bool
page_rec_distance_is_at_most(
/*=========================*/
	const rec_t*	left_rec,
	const rec_t*	right_rec,
	ulint		val)
{
	for (ulint i = 0; i <= val; i++) {
		if (left_rec == right_rec) {
			return (true);
		}
		left_rec = page_rec_get_next_const(left_rec);
	}
	return (false);
}

/************************************************************//**
true if the record is the second last user record on a page.
@return true if the second last user record */
UNIV_INLINE
bool
page_rec_is_second_last(
/*====================*/
	const rec_t*	rec,	/*!< in: record */
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page_get_n_recs(page) > 1);
	ut_ad(!page_rec_is_last(rec, page));

	return(page_rec_get_next_const(
		page_rec_get_next_const(rec)) == page_get_supremum_rec(page));
}

/************************************************************//**
Returns the nth record of the record list.
This is the inverse function of page_rec_get_n_recs_before().
@return nth record */
UNIV_INLINE
rec_t*
page_rec_get_nth(
/*=============*/
	page_t*	page,	/*!< in: page */
	ulint	nth)	/*!< in: nth record */
{
	return((rec_t*) page_rec_get_nth_const(page, nth));
}

/************************************************************//**
Returns the middle record of the records on the page. If there is an
even number of records in the list, returns the first record of the
upper half-list.
@return middle record */
UNIV_INLINE
rec_t*
page_get_middle_rec(
/*================*/
	page_t*	page)	/*!< in: page */
{
	ulint	middle = (ulint(page_get_n_recs(page))
			  + PAGE_HEAP_NO_USER_LOW) / 2;

	return(page_rec_get_nth(page, middle));
}

#endif /* !UNIV_INNOCHECKSUM */

/*************************************************************//**
Gets the page number.
@return page number */
UNIV_INLINE
uint32_t
page_get_page_no(
/*=============*/
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page == page_align((page_t*) page));
	return(mach_read_from_4(page + FIL_PAGE_OFFSET));
}

#ifndef UNIV_INNOCHECKSUM
/*************************************************************//**
Gets the tablespace identifier.
@return space id */
UNIV_INLINE
uint32_t
page_get_space_id(
/*==============*/
	const page_t*	page)	/*!< in: page */
{
	ut_ad(page == page_align((page_t*) page));
	return(mach_read_from_4(page + FIL_PAGE_ARCH_LOG_NO_OR_SPACE_ID));
}

#endif /* !UNIV_INNOCHECKSUM */

/*************************************************************//**
Gets the number of user records on page (infimum and supremum records
are not user records).
@return number of user records */
UNIV_INLINE
uint16_t
page_get_n_recs(
/*============*/
	const page_t*	page)	/*!< in: index page */
{
	return(page_header_get_field(page, PAGE_N_RECS));
}

#ifndef UNIV_INNOCHECKSUM
/*************************************************************//**
Gets the number of dir slots in directory.
@return number of slots */
UNIV_INLINE
uint16_t
page_dir_get_n_slots(
/*=================*/
	const page_t*	page)	/*!< in: index page */
{
	return(page_header_get_field(page, PAGE_N_DIR_SLOTS));
}
/*************************************************************//**
Sets the number of dir slots in directory. */
UNIV_INLINE
void
page_dir_set_n_slots(
/*=================*/
	page_t*		page,	/*!< in/out: page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL */
	ulint		n_slots)/*!< in: number of slots */
{
	page_header_set_field(page, page_zip, PAGE_N_DIR_SLOTS, n_slots);
}

/*************************************************************//**
Gets the number of records in the heap.
@return number of user records */
UNIV_INLINE
uint16_t
page_dir_get_n_heap(
/*================*/
	const page_t*	page)	/*!< in: index page */
{
	return(page_header_get_field(page, PAGE_N_HEAP) & 0x7fff);
}

/*************************************************************//**
Sets the number of records in the heap. */
UNIV_INLINE
void
page_dir_set_n_heap(
/*================*/
	page_t*		page,	/*!< in/out: index page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page whose
				uncompressed part will be updated, or NULL.
				Note that the size of the dense page directory
				in the compressed page trailer is
				n_heap * PAGE_ZIP_DIR_SLOT_SIZE. */
	ulint		n_heap)	/*!< in: number of records */
{
	ut_ad(n_heap < 0x8000);
	ut_ad(!page_zip || uint16_t(n_heap)
	      == (page_header_get_field(page, PAGE_N_HEAP) & 0x7fff) + 1);

	page_header_set_field(page, page_zip, PAGE_N_HEAP, n_heap
			      | (0x8000
				 & page_header_get_field(page, PAGE_N_HEAP)));
}

#ifdef UNIV_DEBUG
/*************************************************************//**
Gets pointer to nth directory slot.
@return pointer to dir slot */
UNIV_INLINE
page_dir_slot_t*
page_dir_get_nth_slot(
/*==================*/
	const page_t*	page,	/*!< in: index page */
	ulint		n)	/*!< in: position */
{
	ut_ad(page_dir_get_n_slots(page) > n);

	return((page_dir_slot_t*)
	       page + srv_page_size - PAGE_DIR
	       - (n + 1) * PAGE_DIR_SLOT_SIZE);
}
#endif /* UNIV_DEBUG */

/**************************************************************//**
Used to check the consistency of a record on a page.
@return TRUE if succeed */
UNIV_INLINE
ibool
page_rec_check(
/*===========*/
	const rec_t*	rec)	/*!< in: record */
{
	const page_t*	page = page_align(rec);

	ut_a(rec);

	ut_a(page_offset(rec) <= page_header_get_field(page, PAGE_HEAP_TOP));
	ut_a(page_offset(rec) >= PAGE_DATA);

	return(TRUE);
}

/***************************************************************//**
Gets the record pointed to by a directory slot.
@return pointer to record */
UNIV_INLINE
const rec_t*
page_dir_slot_get_rec(
/*==================*/
	const page_dir_slot_t*	slot)	/*!< in: directory slot */
{
	return(page_align(slot) + mach_read_from_2(slot));
}

/***************************************************************//**
This is used to set the record offset in a directory slot. */
UNIV_INLINE
void
page_dir_slot_set_rec(
/*==================*/
	page_dir_slot_t* slot,	/*!< in: directory slot */
	rec_t*		 rec)	/*!< in: record on the page */
{
	ut_ad(page_rec_check(rec));

	mach_write_to_2(slot, page_offset(rec));
}

/***************************************************************//**
Gets the number of records owned by a directory slot.
@return number of records */
UNIV_INLINE
ulint
page_dir_slot_get_n_owned(
/*======================*/
	const page_dir_slot_t*	slot)	/*!< in: page directory slot */
{
	const rec_t*	rec	= page_dir_slot_get_rec(slot);
	if (page_rec_is_comp(slot)) {
		return(rec_get_n_owned_new(rec));
	} else {
		return(rec_get_n_owned_old(rec));
	}
}

/***************************************************************//**
This is used to set the owned records field of a directory slot. */
UNIV_INLINE
void
page_dir_slot_set_n_owned(
/*======================*/
	page_dir_slot_t*slot,	/*!< in/out: directory slot */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page, or NULL */
	ulint		n)	/*!< in: number of records owned by the slot */
{
	rec_t*	rec	= (rec_t*) page_dir_slot_get_rec(slot);
	if (page_rec_is_comp(slot)) {
		rec_set_n_owned_new(rec, page_zip, n);
	} else {
		ut_ad(!page_zip);
		rec_set_n_owned_old(rec, n);
	}
}

/************************************************************//**
Calculates the space reserved for directory slots of a given number of
records. The exact value is a fraction number n * PAGE_DIR_SLOT_SIZE /
PAGE_DIR_SLOT_MIN_N_OWNED, and it is rounded upwards to an integer. */
UNIV_INLINE
ulint
page_dir_calc_reserved_space(
/*=========================*/
	ulint	n_recs)		/*!< in: number of records */
{
	return((PAGE_DIR_SLOT_SIZE * n_recs + PAGE_DIR_SLOT_MIN_N_OWNED - 1)
	       / PAGE_DIR_SLOT_MIN_N_OWNED);
}

/************************************************************//**
Gets the pointer to the next record on the page.
@return pointer to next record */
UNIV_INLINE
const rec_t*
page_rec_get_next_low(
/*==================*/
	const rec_t*	rec,	/*!< in: pointer to record */
	ulint		comp)	/*!< in: nonzero=compact page layout */
{
	ulint		offs;
	const page_t*	page;

	ut_ad(page_rec_check(rec));

	page = page_align(rec);

	offs = rec_get_next_offs(rec, comp);

	if (offs >= srv_page_size) {
		fprintf(stderr,
			"InnoDB: Next record offset is nonsensical %lu"
			" in record at offset %lu\n"
			"InnoDB: rec address %p, space id %lu, page %lu\n",
			(ulong) offs, (ulong) page_offset(rec),
			(void*) rec,
			(ulong) page_get_space_id(page),
			(ulong) page_get_page_no(page));
		ut_error;
	} else if (offs == 0) {

		return(NULL);
	}

	ut_ad(page_rec_is_infimum(rec)
	      || (!page_is_leaf(page) && !page_has_prev(page))
	      || !(rec_get_info_bits(page + offs, comp)
		   & REC_INFO_MIN_REC_FLAG));

	return(page + offs);
}

/************************************************************//**
Gets the pointer to the next record on the page.
@return pointer to next record */
UNIV_INLINE
rec_t*
page_rec_get_next(
/*==============*/
	rec_t*	rec)	/*!< in: pointer to record */
{
	return((rec_t*) page_rec_get_next_low(rec, page_rec_is_comp(rec)));
}

/************************************************************//**
Gets the pointer to the next record on the page.
@return pointer to next record */
UNIV_INLINE
const rec_t*
page_rec_get_next_const(
/*====================*/
	const rec_t*	rec)	/*!< in: pointer to record */
{
	return(page_rec_get_next_low(rec, page_rec_is_comp(rec)));
}

/************************************************************//**
Gets the pointer to the next non delete-marked record on the page.
If all subsequent records are delete-marked, then this function
will return the supremum record.
@return pointer to next non delete-marked record or pointer to supremum */
UNIV_INLINE
const rec_t*
page_rec_get_next_non_del_marked(
/*=============================*/
	const rec_t*	rec)	/*!< in: pointer to record */
{
	const rec_t*	r;
	ulint		page_is_compact = page_rec_is_comp(rec);

	for (r = page_rec_get_next_const(rec);
	     !page_rec_is_supremum(r)
	     && rec_get_deleted_flag(r, page_is_compact);
	     r = page_rec_get_next_const(r)) {
		/* noop */
	}

	return(r);
}

/************************************************************//**
Sets the pointer to the next record on the page. */
UNIV_INLINE
void
page_rec_set_next(
/*==============*/
	rec_t*		rec,	/*!< in: pointer to record,
				must not be page supremum */
	const rec_t*	next)	/*!< in: pointer to next record,
				must not be page infimum */
{
	ulint	offs;

	ut_ad(page_rec_check(rec));
	ut_ad(!page_rec_is_supremum(rec));
	ut_ad(rec != next);

	ut_ad(!next || !page_rec_is_infimum(next));
	ut_ad(!next || page_align(rec) == page_align(next));

	offs = next != NULL ? page_offset(next) : 0;

	if (page_rec_is_comp(rec)) {
		rec_set_next_offs_new(rec, offs);
	} else {
		rec_set_next_offs_old(rec, offs);
	}
}

/************************************************************//**
Gets the pointer to the previous record.
@return pointer to previous record */
UNIV_INLINE
const rec_t*
page_rec_get_prev_const(
/*====================*/
	const rec_t*	rec)	/*!< in: pointer to record, must not be page
				infimum */
{
	const page_dir_slot_t*	slot;
	ulint			slot_no;
	const rec_t*		rec2;
	const rec_t*		prev_rec = NULL;
	const page_t*		page;

	ut_ad(page_rec_check(rec));

	page = page_align(rec);

	ut_ad(!page_rec_is_infimum(rec));

	slot_no = page_dir_find_owner_slot(rec);

	ut_a(slot_no != 0);

	slot = page_dir_get_nth_slot(page, slot_no - 1);

	rec2 = page_dir_slot_get_rec(slot);

	if (page_is_comp(page)) {
		while (rec != rec2) {
			prev_rec = rec2;
			rec2 = page_rec_get_next_low(rec2, TRUE);
		}
	} else {
		while (rec != rec2) {
			prev_rec = rec2;
			rec2 = page_rec_get_next_low(rec2, FALSE);
		}
	}

	ut_a(prev_rec);

	return(prev_rec);
}

/************************************************************//**
Gets the pointer to the previous record.
@return pointer to previous record */
UNIV_INLINE
rec_t*
page_rec_get_prev(
/*==============*/
	rec_t*	rec)	/*!< in: pointer to record, must not be page
			infimum */
{
	return((rec_t*) page_rec_get_prev_const(rec));
}

/***************************************************************//**
Looks for the record which owns the given record.
@return the owner record */
UNIV_INLINE
rec_t*
page_rec_find_owner_rec(
/*====================*/
	rec_t*	rec)	/*!< in: the physical record */
{
	ut_ad(page_rec_check(rec));

	if (page_rec_is_comp(rec)) {
		while (rec_get_n_owned_new(rec) == 0) {
			rec = page_rec_get_next(rec);
		}
	} else {
		while (rec_get_n_owned_old(rec) == 0) {
			rec = page_rec_get_next(rec);
		}
	}

	return(rec);
}

/**********************************************************//**
Returns the base extra size of a physical record.  This is the
size of the fixed header, independent of the record size.
@return REC_N_NEW_EXTRA_BYTES or REC_N_OLD_EXTRA_BYTES */
UNIV_INLINE
ulint
page_rec_get_base_extra_size(
/*=========================*/
	const rec_t*	rec)	/*!< in: physical record */
{
	compile_time_assert(REC_N_NEW_EXTRA_BYTES + 1
			    == REC_N_OLD_EXTRA_BYTES);
	return(REC_N_NEW_EXTRA_BYTES + (ulint) !page_rec_is_comp(rec));
}

#endif /* UNIV_INNOCHECKSUM */

/************************************************************//**
Returns the sum of the sizes of the records in the record list, excluding
the infimum and supremum records.
@return data in bytes */
UNIV_INLINE
uint16_t
page_get_data_size(
/*===============*/
	const page_t*	page)	/*!< in: index page */
{
	uint16_t	ret = page_header_get_field(page, PAGE_HEAP_TOP)
		- (page_is_comp(page)
		   ? PAGE_NEW_SUPREMUM_END
		   : PAGE_OLD_SUPREMUM_END)
		- page_header_get_field(page, PAGE_GARBAGE);
	ut_ad(ret < srv_page_size);
	return(ret);
}

#ifndef UNIV_INNOCHECKSUM
/************************************************************//**
Allocates a block of memory from the free list of an index page. */
UNIV_INLINE
void
page_mem_alloc_free(
/*================*/
	page_t*		page,	/*!< in/out: index page */
	page_zip_des_t*	page_zip,/*!< in/out: compressed page with enough
				space available for inserting the record,
				or NULL */
	rec_t*		next_rec,/*!< in: pointer to the new head of the
				free record list */
	ulint		need)	/*!< in: number of bytes allocated */
{
	ulint		garbage;

#ifdef UNIV_DEBUG
	const rec_t*	old_rec	= page_header_get_ptr(page, PAGE_FREE);
	ulint		next_offs;

	ut_ad(old_rec);
	next_offs = rec_get_next_offs(old_rec, page_is_comp(page));
	ut_ad(next_rec == (next_offs ? page + next_offs : NULL));
#endif

	page_header_set_ptr(page, page_zip, PAGE_FREE, next_rec);

	garbage = page_header_get_field(page, PAGE_GARBAGE);
	ut_ad(garbage >= need);

	page_header_set_field(page, page_zip, PAGE_GARBAGE, garbage - need);
}

/*************************************************************//**
Calculates free space if a page is emptied.
@return free space */
UNIV_INLINE
ulint
page_get_free_space_of_empty(
/*=========================*/
	ulint	comp)		/*!< in: nonzero=compact page layout */
{
	if (comp) {
		return((ulint)(srv_page_size
			       - PAGE_NEW_SUPREMUM_END
			       - PAGE_DIR
			       - 2 * PAGE_DIR_SLOT_SIZE));
	}

	return((ulint)(srv_page_size
		       - PAGE_OLD_SUPREMUM_END
		       - PAGE_DIR
		       - 2 * PAGE_DIR_SLOT_SIZE));
}

/************************************************************//**
Each user record on a page, and also the deleted user records in the heap
takes its size plus the fraction of the dir cell size /
PAGE_DIR_SLOT_MIN_N_OWNED bytes for it. If the sum of these exceeds the
value of page_get_free_space_of_empty, the insert is impossible, otherwise
it is allowed. This function returns the maximum combined size of records
which can be inserted on top of the record heap.
@return maximum combined size for inserted records */
UNIV_INLINE
ulint
page_get_max_insert_size(
/*=====================*/
	const page_t*	page,	/*!< in: index page */
	ulint		n_recs)	/*!< in: number of records */
{
	ulint	occupied;
	ulint	free_space;

	if (page_is_comp(page)) {
		occupied = page_header_get_field(page, PAGE_HEAP_TOP)
			- PAGE_NEW_SUPREMUM_END
			+ page_dir_calc_reserved_space(
				n_recs + page_dir_get_n_heap(page) - 2);

		free_space = page_get_free_space_of_empty(TRUE);
	} else {
		occupied = page_header_get_field(page, PAGE_HEAP_TOP)
			- PAGE_OLD_SUPREMUM_END
			+ page_dir_calc_reserved_space(
				n_recs + page_dir_get_n_heap(page) - 2);

		free_space = page_get_free_space_of_empty(FALSE);
	}

	/* Above the 'n_recs +' part reserves directory space for the new
	inserted records; the '- 2' excludes page infimum and supremum
	records */

	if (occupied > free_space) {

		return(0);
	}

	return(free_space - occupied);
}

/************************************************************//**
Returns the maximum combined size of records which can be inserted on top
of the record heap if a page is first reorganized.
@return maximum combined size for inserted records */
UNIV_INLINE
ulint
page_get_max_insert_size_after_reorganize(
/*======================================*/
	const page_t*	page,	/*!< in: index page */
	ulint		n_recs)	/*!< in: number of records */
{
	ulint	occupied;
	ulint	free_space;

	occupied = page_get_data_size(page)
		+ page_dir_calc_reserved_space(n_recs + page_get_n_recs(page));

	free_space = page_get_free_space_of_empty(page_is_comp(page));

	if (occupied > free_space) {

		return(0);
	}

	return(free_space - occupied);
}

/************************************************************//**
Puts a record to free list. */
UNIV_INLINE
void
page_mem_free(
/*==========*/
	page_t*			page,		/*!< in/out: index page */
	page_zip_des_t*		page_zip,	/*!< in/out: compressed page,
						or NULL */
	rec_t*			rec,		/*!< in: pointer to the
						(origin of) record */
	const dict_index_t*	index,		/*!< in: index of rec */
	const rec_offs*		offsets)	/*!< in: array returned by
						rec_get_offsets() */
{
	rec_t*		free;
	ulint		garbage;

	ut_ad(rec_offs_validate(rec, index, offsets));
	free = page_header_get_ptr(page, PAGE_FREE);

	if (srv_immediate_scrub_data_uncompressed) {
		/* scrub record */
		memset(rec, 0, rec_offs_data_size(offsets));
	}

	page_rec_set_next(rec, free);
	page_header_set_ptr(page, page_zip, PAGE_FREE, rec);

	garbage = page_header_get_field(page, PAGE_GARBAGE);

	page_header_set_field(page, page_zip, PAGE_GARBAGE,
			      garbage + rec_offs_size(offsets));

	if (page_zip) {
		page_zip_dir_delete(page_zip, rec, index, offsets, free);
	} else {
		page_header_set_field(page, page_zip, PAGE_N_RECS,
				      ulint(page_get_n_recs(page)) - 1);
	}
}

/** Read the PAGE_DIRECTION field from a byte.
@param[in]	ptr	pointer to PAGE_DIRECTION_B
@return	the value of the PAGE_DIRECTION field */
inline
byte
page_ptr_get_direction(const byte* ptr)
{
	ut_ad(page_offset(ptr) == PAGE_HEADER + PAGE_DIRECTION_B);
	return *ptr & ((1U << 3) - 1);
}

/** Set the PAGE_DIRECTION field.
@param[in]	ptr	pointer to PAGE_DIRECTION_B
@param[in]	dir	the value of the PAGE_DIRECTION field */
inline
void
page_ptr_set_direction(byte* ptr, byte dir)
{
	ut_ad(page_offset(ptr) == PAGE_HEADER + PAGE_DIRECTION_B);
	ut_ad(dir >= PAGE_LEFT);
	ut_ad(dir <= PAGE_NO_DIRECTION);
	*ptr = (*ptr & ~((1U << 3) - 1)) | dir;
}

/** Read the PAGE_INSTANT field.
@param[in]	page	index page
@return the value of the PAGE_INSTANT field */
inline
uint16_t
page_get_instant(const page_t* page)
{
	uint16_t i = page_header_get_field(page, PAGE_INSTANT);
#ifdef UNIV_DEBUG
	switch (fil_page_get_type(page)) {
	case FIL_PAGE_TYPE_INSTANT:
		ut_ad(page_get_direction(page) <= PAGE_NO_DIRECTION);
		ut_ad(i >> 3);
		break;
	case FIL_PAGE_INDEX:
		ut_ad(i <= PAGE_NO_DIRECTION || !page_is_comp(page));
		break;
	case FIL_PAGE_RTREE:
		ut_ad(i <= PAGE_NO_DIRECTION);
		break;
	default:
		ut_ad(!"invalid page type");
		break;
	}
#endif /* UNIV_DEBUG */
	return static_cast<uint16_t>(i >> 3);  /* i / 8 */
}
#endif /* !UNIV_INNOCHECKSUM */

#endif

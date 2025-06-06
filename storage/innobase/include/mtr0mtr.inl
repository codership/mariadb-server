/*****************************************************************************

Copyright (c) 1995, 2014, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, 2021, MariaDB Corporation.

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
@file include/mtr0mtr.ic
Mini-transaction buffer

Created 11/26/1995 Heikki Tuuri
*******************************************************/

#include "buf0buf.h"

/** Check if a mini-transaction is dirtying a clean page.
@return true if the mtr is dirtying a clean page. */
bool
mtr_t::is_block_dirtied(const buf_block_t* block)
{
	ut_ad(buf_block_get_state(block) == BUF_BLOCK_FILE_PAGE);
	ut_ad(block->page.buf_fix_count > 0);

	/* It is OK to read oldest_modification because no
	other thread can be performing a write of it and it
	is only during write that the value is reset to 0. */
	return(block->page.oldest_modification == 0);
}

/**
Pushes an object to an mtr memo stack. */
void
mtr_t::memo_push(void* object, mtr_memo_type_t type)
{
	ut_ad(is_active());
	ut_ad(object != NULL);
	ut_ad(type >= MTR_MEMO_PAGE_S_FIX);
	ut_ad(type <= MTR_MEMO_SPACE_X_LOCK);
	ut_ad(ut_is_2pow(type));

	/* If this mtr has x-fixed a clean page then we set
	the made_dirty flag. This tells us if we need to
	grab log_sys.flush_order_mutex at mtr_t::commit() so that we
	can insert the dirtied page into the flush list. */

	if ((type == MTR_MEMO_PAGE_X_FIX || type == MTR_MEMO_PAGE_SX_FIX)
	    && !m_made_dirty) {

		m_made_dirty = is_block_dirtied(
			reinterpret_cast<const buf_block_t*>(object));
	}

	mtr_memo_slot_t* slot = m_memo.push<mtr_memo_slot_t*>(sizeof(*slot));

	slot->type = type;
	slot->object = object;
}

/**
Releases the (index tree) s-latch stored in an mtr memo after a
savepoint. */
void
mtr_t::release_s_latch_at_savepoint(
	ulint		savepoint,
	rw_lock_t*	lock)
{
	ut_ad(is_active());
	ut_ad(m_memo.size() > savepoint);

	mtr_memo_slot_t* slot = m_memo.at<mtr_memo_slot_t*>(savepoint);

	ut_ad(slot->object == lock);
	ut_ad(slot->type == MTR_MEMO_S_LOCK);

	rw_lock_s_unlock(lock);

	slot->object = NULL;
}

/**
SX-latches the not yet latched block after a savepoint. */

void
mtr_t::sx_latch_at_savepoint(
	ulint		savepoint,
	buf_block_t*	block)
{
	ut_ad(is_active());
	ut_ad(m_memo.size() > savepoint);

	ut_ad(!memo_contains_flagged(
			block,
			MTR_MEMO_PAGE_S_FIX
			| MTR_MEMO_PAGE_X_FIX
			| MTR_MEMO_PAGE_SX_FIX));

	mtr_memo_slot_t* slot = m_memo.at<mtr_memo_slot_t*>(savepoint);

	ut_ad(slot->object == block);

	/* == RW_NO_LATCH */
	ut_a(slot->type == MTR_MEMO_BUF_FIX);

	rw_lock_sx_lock(&block->lock);

	if (!m_made_dirty) {
		m_made_dirty = is_block_dirtied(block);
	}

	slot->type = MTR_MEMO_PAGE_SX_FIX;
}

/**
X-latches the not yet latched block after a savepoint. */

void
mtr_t::x_latch_at_savepoint(
	ulint		savepoint,
	buf_block_t*	block)
{
	ut_ad(is_active());
	ut_ad(m_memo.size() > savepoint);

	ut_ad(!memo_contains_flagged(
			block,
			MTR_MEMO_PAGE_S_FIX
			| MTR_MEMO_PAGE_X_FIX
			| MTR_MEMO_PAGE_SX_FIX));

	mtr_memo_slot_t* slot = m_memo.at<mtr_memo_slot_t*>(savepoint);

	ut_ad(slot->object == block);

	/* == RW_NO_LATCH */
	ut_a(slot->type == MTR_MEMO_BUF_FIX);

	rw_lock_x_lock(&block->lock);

	if (!m_made_dirty) {
		m_made_dirty = is_block_dirtied(block);
	}

	slot->type = MTR_MEMO_PAGE_X_FIX;
}

/**
Releases the block in an mtr memo after a savepoint. */

void
mtr_t::release_block_at_savepoint(
	ulint		savepoint,
	buf_block_t*	block)
{
	ut_ad(is_active());

	mtr_memo_slot_t* slot = m_memo.at<mtr_memo_slot_t*>(savepoint);

	ut_a(slot->object == block);

	buf_page_release_latch(block, slot->type);

	reinterpret_cast<buf_block_t*>(block)->unfix();

	slot->object = NULL;
}

/**
Gets the logging mode of a mini-transaction.
@return	logging mode: MTR_LOG_NONE, ... */

mtr_log_t
mtr_t::get_log_mode() const
{
	ut_ad(m_log_mode >= MTR_LOG_ALL);
	ut_ad(m_log_mode <= MTR_LOG_SHORT_INSERTS);

	return m_log_mode;
}

/**
Changes the logging mode of a mini-transaction.
@return	old mode */

mtr_log_t
mtr_t::set_log_mode(mtr_log_t mode)
{
	ut_ad(mode >= MTR_LOG_ALL);
	ut_ad(mode <= MTR_LOG_SHORT_INSERTS);

	const mtr_log_t	old_mode = m_log_mode;

	switch (old_mode) {
	case MTR_LOG_NO_REDO:
		/* Once this mode is set, it must not be changed. */
		ut_ad(mode == MTR_LOG_NO_REDO || mode == MTR_LOG_NONE);
		return(old_mode);
	case MTR_LOG_NONE:
		if (mode == old_mode || mode == MTR_LOG_SHORT_INSERTS) {
			/* Keep MTR_LOG_NONE. */
			return(old_mode);
		}
		/* fall through */
	case MTR_LOG_SHORT_INSERTS:
		ut_ad(mode == MTR_LOG_ALL);
		/* fall through */
	case MTR_LOG_ALL:
		/* MTR_LOG_NO_REDO can only be set before generating
		any redo log records. */
		ut_ad(mode != MTR_LOG_NO_REDO || m_n_log_recs == 0);
		m_log_mode = mode;
		return(old_mode);
	}

	ut_ad(0);
	return(old_mode);
}

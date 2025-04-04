/*****************************************************************************

Copyright (c) 1996, 2015, Oracle and/or its affiliates. All Rights Reserved.
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
@file include/trx0trx.ic
The transaction

Created 3/26/1996 Heikki Tuuri
*******************************************************/

/**********************************************************************//**
Determines if a transaction is in the given state.
The caller must hold trx_sys.mutex, or it must be the thread
that is serving a running transaction.
A running RW transaction must be in trx_sys.rw_trx_hash.
@return TRUE if trx->state == state */
UNIV_INLINE
bool
trx_state_eq(
/*=========*/
	const trx_t*	trx,	/*!< in: transaction */
	trx_state_t	state,	/*!< in: state;
				if state != TRX_STATE_NOT_STARTED
				asserts that
				trx->state != TRX_STATE_NOT_STARTED */
	bool		relaxed)
				/*!< in: whether to allow
				trx->state == TRX_STATE_NOT_STARTED
				after an error has been reported */
{
#ifdef UNIV_DEBUG
	switch (trx->state) {
	case TRX_STATE_PREPARED:
	case TRX_STATE_PREPARED_RECOVERED:
	case TRX_STATE_COMMITTED_IN_MEMORY:
		ut_ad(!trx->is_autocommit_non_locking());
		return(trx->state == state);

	case TRX_STATE_ACTIVE:
		if (trx->is_autocommit_non_locking()) {
			ut_ad(!trx->is_recovered);
			ut_ad(trx->read_only);
			ut_ad(trx->mysql_thd);
		}
		return(state == trx->state);

	case TRX_STATE_NOT_STARTED:
		/* These states are not allowed for running transactions. */
		ut_a(state == TRX_STATE_NOT_STARTED
		     || (relaxed
			 && thd_get_error_number(trx->mysql_thd)));

		return(true);
	}
	ut_error;
#endif /* UNIV_DEBUG */
	return(trx->state == state);
}

/****************************************************************//**
Retrieves the error_info field from a trx.
@return the error info */
UNIV_INLINE
const dict_index_t*
trx_get_error_info(
/*===============*/
	const trx_t*	trx)	/*!< in: trx object */
{
	return(trx->error_info);
}

/*******************************************************************//**
Retrieves transaction's que state in a human readable string. The string
should not be free()'d or modified.
@return string in the data segment */
UNIV_INLINE
const char*
trx_get_que_state_str(
/*==================*/
	const trx_t*	trx)	/*!< in: transaction */
{
	/* be sure to adjust TRX_QUE_STATE_STR_MAX_LEN if you change this */
	switch (trx->lock.que_state) {
	case TRX_QUE_RUNNING:
		return("RUNNING");
	case TRX_QUE_LOCK_WAIT:
		return("LOCK WAIT");
	case TRX_QUE_ROLLING_BACK:
		return("ROLLING BACK");
	case TRX_QUE_COMMITTING:
		return("COMMITTING");
	default:
		return("UNKNOWN");
	}
}

/** Retreieves the transaction ID.
In a given point in time it is guaranteed that IDs of the running
transactions are unique. The values returned by this function for readonly
transactions may be reused, so a subsequent RO transaction may get the same ID
as a RO transaction that existed in the past. The values returned by this
function should be used for printing purposes only.
@param[in]	trx	transaction whose id to retrieve
@return transaction id */
UNIV_INLINE
trx_id_t
trx_get_id_for_print(
	const trx_t*	trx)
{
	/* Readonly and transactions whose intentions are unknown (whether
	they will eventually do a WRITE) don't have trx_t::id assigned (it is
	0 for those transactions). Transaction IDs in
	innodb_trx.trx_id,
	innodb_locks.lock_id,
	innodb_locks.lock_trx_id,
	innodb_lock_waits.requesting_trx_id,
	innodb_lock_waits.blocking_trx_id should match because those tables
	could be used in an SQL JOIN on those columns. Also trx_t::id is
	printed by SHOW ENGINE INNODB STATUS, and in logs, so we must have the
	same value printed everywhere consistently. */

	/* DATA_TRX_ID_LEN is the storage size in bytes. */
	static const trx_id_t	max_trx_id
		= (1ULL << (DATA_TRX_ID_LEN * CHAR_BIT)) - 1;

	ut_ad(trx->id <= max_trx_id);

	return(trx->id != 0
	       ? trx->id
	       : reinterpret_cast<trx_id_t>(trx) | (max_trx_id + 1));
}

/**********************************************************************//**
Determine if a transaction is a dictionary operation.
@return dictionary operation mode */
UNIV_INLINE
enum trx_dict_op_t
trx_get_dict_operation(
/*===================*/
	const trx_t*	trx)	/*!< in: transaction */
{
	trx_dict_op_t op = static_cast<trx_dict_op_t>(trx->dict_operation);

#ifdef UNIV_DEBUG
	switch (op) {
	case TRX_DICT_OP_NONE:
	case TRX_DICT_OP_TABLE:
	case TRX_DICT_OP_INDEX:
		return(op);
	}
	ut_error;
#endif /* UNIV_DEBUG */
	return(op);
}
/**********************************************************************//**
Flag a transaction a dictionary operation. */
UNIV_INLINE
void
trx_set_dict_operation(
/*===================*/
	trx_t*			trx,	/*!< in/out: transaction */
	enum trx_dict_op_t	op)	/*!< in: operation, not
					TRX_DICT_OP_NONE */
{
#ifdef UNIV_DEBUG
	enum trx_dict_op_t	old_op = trx_get_dict_operation(trx);

	switch (op) {
	case TRX_DICT_OP_NONE:
		ut_error;
		break;
	case TRX_DICT_OP_TABLE:
		switch (old_op) {
		case TRX_DICT_OP_NONE:
		case TRX_DICT_OP_INDEX:
		case TRX_DICT_OP_TABLE:
			goto ok;
		}
		ut_error;
		break;
	case TRX_DICT_OP_INDEX:
		ut_ad(old_op == TRX_DICT_OP_NONE);
		break;
	}
ok:
#endif /* UNIV_DEBUG */

	trx->ddl = true;
	trx->dict_operation = op;
}

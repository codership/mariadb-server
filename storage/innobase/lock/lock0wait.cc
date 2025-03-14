/*****************************************************************************

Copyright (c) 1996, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2014, 2021, MariaDB Corporation.

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
@file lock/lock0wait.cc
The transaction lock system

Created 25/5/2010 Sunny Bains
*******************************************************/

#define LOCK_MODULE_IMPLEMENTATION

#include "univ.i"
#include <mysql/service_thd_wait.h>
#include <mysql/service_wsrep.h>

#include "srv0mon.h"
#include "que0que.h"
#include "lock0lock.h"
#include "row0mysql.h"
#include "srv0start.h"
#include "lock0priv.h"

/*********************************************************************//**
Print the contents of the lock_sys_t::waiting_threads array. */
static
void
lock_wait_table_print(void)
/*=======================*/
{
	ut_ad(lock_wait_mutex_own());

	const srv_slot_t*	slot = lock_sys.waiting_threads;

	for (ulint i = 0; i < srv_max_n_threads; i++, ++slot) {

		fprintf(stderr,
			"Slot %lu: thread type %lu,"
			" in use %lu, susp %lu, timeout %lu, time %lu\n",
			(ulong) i,
			(ulong) slot->type,
			(ulong) slot->in_use,
			(ulong) slot->suspended,
			slot->wait_timeout,
			(ulong) difftime(time(NULL), slot->suspend_time));
	}
}

/*********************************************************************//**
Release a slot in the lock_sys_t::waiting_threads. Adjust the array last pointer
if there are empty slots towards the end of the table. */
static
void
lock_wait_table_release_slot(
/*=========================*/
	srv_slot_t*	slot)		/*!< in: slot to release */
{
#ifdef UNIV_DEBUG
	srv_slot_t*	upper = lock_sys.waiting_threads + srv_max_n_threads;
#endif /* UNIV_DEBUG */

	lock_wait_mutex_enter();

	ut_ad(slot->in_use);
	ut_ad(slot->thr != NULL);
	ut_ad(slot->thr->slot != NULL);
	ut_ad(slot->thr->slot == slot);

	/* Must be within the array boundaries. */
	ut_ad(slot >= lock_sys.waiting_threads);
	ut_ad(slot < upper);

	/* Note: When we reserve the slot we use the trx_t::mutex to update
	the slot values to change the state to reserved. Here we are using the
	lock mutex to change the state of the slot to free. This is by design,
	because when we query the slot state we always hold both the lock and
	trx_t::mutex. To reduce contention on the lock mutex when reserving the
	slot we avoid acquiring the lock mutex. */

	lock_mutex_enter();

	slot->thr->slot = NULL;
	slot->thr = NULL;
	slot->in_use = FALSE;

	lock_mutex_exit();

	/* Scan backwards and adjust the last free slot pointer. */
	for (slot = lock_sys.last_slot;
	     slot > lock_sys.waiting_threads && !slot->in_use;
	     --slot) {
		/* No op */
	}

	/* Either the array is empty or the last scanned slot is in use. */
	ut_ad(slot->in_use || slot == lock_sys.waiting_threads);

	lock_sys.last_slot = slot + 1;

	/* The last slot is either outside of the array boundary or it's
	on an empty slot. */
	ut_ad(lock_sys.last_slot == upper || !lock_sys.last_slot->in_use);

	ut_ad(lock_sys.last_slot >= lock_sys.waiting_threads);
	ut_ad(lock_sys.last_slot <= upper);

	lock_wait_mutex_exit();
}

/*********************************************************************//**
Reserves a slot in the thread table for the current user OS thread.
@return reserved slot */
static
srv_slot_t*
lock_wait_table_reserve_slot(
/*=========================*/
	que_thr_t*	thr,		/*!< in: query thread associated
					with the user OS thread */
	ulong		wait_timeout)	/*!< in: lock wait timeout value */
{
	ulint		i;
	srv_slot_t*	slot;

	ut_ad(lock_wait_mutex_own());
	ut_ad(trx_mutex_own(thr_get_trx(thr)));

	slot = lock_sys.waiting_threads;

	for (i = srv_max_n_threads; i--; ++slot) {
		if (!slot->in_use) {
			slot->in_use = TRUE;
			slot->thr = thr;
			slot->thr->slot = slot;

			if (slot->event == NULL) {
				slot->event = os_event_create(0);
				ut_a(slot->event);
			}

			os_event_reset(slot->event);
			slot->suspended = TRUE;
			slot->suspend_time = time(NULL);
			slot->wait_timeout = wait_timeout;

			if (slot == lock_sys.last_slot) {
				++lock_sys.last_slot;
			}

			ut_ad(lock_sys.last_slot
			      <= lock_sys.waiting_threads + srv_max_n_threads);

			return(slot);
		}
	}

	ib::error() << "There appear to be " << srv_max_n_threads << " user"
		" threads currently waiting inside InnoDB, which is the upper"
		" limit. Cannot continue operation. Before aborting, we print"
		" a list of waiting threads.";
	lock_wait_table_print();

	ut_error;
	return(NULL);
}

#ifdef WITH_WSREP
/*********************************************************************//**
check if lock timeout was for priority thread,
as a side effect trigger lock monitor
@param[in]    trx    transaction owning the lock
@return	false for regular lock timeout */
static
bool
wsrep_is_BF_lock_timeout(
	const trx_t*	trx)
{
	bool long_wait= (trx->error_state != DB_DEADLOCK &&
			 trx->is_wsrep() &&
			 wsrep_thd_is_BF(trx->mysql_thd, false));
	bool was_wait= true;

	DBUG_EXECUTE_IF("wsrep_instrument_BF_lock_wait",
			was_wait=false; long_wait=true;);

	if (long_wait) {
		ib::info() << "WSREP: BF lock wait long for trx:" << trx->id
			   << " query: " << wsrep_thd_query(trx->mysql_thd);

		return was_wait;
	} else {
		return false;
	}
}
#endif /* WITH_WSREP */

/***************************************************************//**
Puts a user OS thread to wait for a lock to be released. If an error
occurs during the wait trx->error_state associated with thr is
!= DB_SUCCESS when we return. DB_LOCK_WAIT_TIMEOUT and DB_DEADLOCK
are possible errors. DB_DEADLOCK is returned if selective deadlock
resolution chose this transaction as a victim. */
void
lock_wait_suspend_thread(
/*=====================*/
	que_thr_t*	thr)	/*!< in: query thread associated with the
				user OS thread */
{
	srv_slot_t*	slot;
	trx_t*		trx;
	ibool		was_declared_inside_innodb;
	ulong		lock_wait_timeout;

	trx = thr_get_trx(thr);

	if (trx->mysql_thd != 0) {
		DEBUG_SYNC_C("lock_wait_suspend_thread_enter");
	}

	/* InnoDB system transactions (such as the purge, and
	incomplete transactions that are being rolled back after crash
	recovery) will use the global value of
	innodb_lock_wait_timeout, because trx->mysql_thd == NULL. */
	lock_wait_timeout = trx_lock_wait_timeout_get(trx);

	lock_wait_mutex_enter();

	trx_mutex_enter(trx);

	trx->error_state = DB_SUCCESS;

	if (thr->state == QUE_THR_RUNNING) {

		ut_ad(thr->is_active);

		/* The lock has already been released or this transaction
		was chosen as a deadlock victim: no need to suspend */

		if (trx->lock.was_chosen_as_deadlock_victim) {

			trx->error_state = DB_DEADLOCK;
			trx->lock.was_chosen_as_deadlock_victim = false;
		}

		lock_wait_mutex_exit();
		trx_mutex_exit(trx);
		return;
	}

	ut_ad(!thr->is_active);

	slot = lock_wait_table_reserve_slot(thr, lock_wait_timeout);

	lock_wait_mutex_exit();
	trx_mutex_exit(trx);

	ulonglong start_time = 0;

	if (thr->lock_state == QUE_THR_LOCK_ROW) {
		srv_stats.n_lock_wait_count.inc();
		srv_stats.n_lock_wait_current_count++;
		start_time = my_interval_timer();
	}

	ulint	lock_type = ULINT_UNDEFINED;
#ifndef DBUG_OFF
	ulint	lock_mode = LOCK_NONE;
#endif
	/* The wait_lock can be cleared by another thread when the
	lock is released. But the wait can only be initiated by the
	current thread which owns the transaction. Only acquire the
	mutex if the wait_lock is still active. */
	if (const lock_t* wait_lock = trx->lock.wait_lock) {
		lock_mutex_enter();
		wait_lock = trx->lock.wait_lock;
		if (wait_lock) {
			lock_type = lock_get_type_low(wait_lock);
#ifndef DBUG_OFF
			lock_mode = lock_get_mode(wait_lock);
#endif
		}
		lock_mutex_exit();
	}

	ulint	had_dict_lock = trx->dict_operation_lock_mode;

	switch (had_dict_lock) {
	case 0:
		break;
	case RW_S_LATCH:
		/* Release foreign key check latch */
		row_mysql_unfreeze_data_dictionary(trx);

		DEBUG_SYNC_C("lock_wait_release_s_latch_before_sleep");
		break;
	default:
		/* There should never be a lock wait when the
		dictionary latch is reserved in X mode.  Dictionary
		transactions should only acquire locks on dictionary
		tables, not other tables. All access to dictionary
		tables should be covered by dictionary
		transactions. */
		ut_error;
	}

	ut_a(trx->dict_operation_lock_mode == 0);

	/* Suspend this thread and wait for the event. */

	was_declared_inside_innodb = trx->declared_to_be_inside_innodb;

	if (was_declared_inside_innodb) {
		/* We must declare this OS thread to exit InnoDB, since a
		possible other thread holding a lock which this thread waits
		for must be allowed to enter, sooner or later */

		srv_conc_force_exit_innodb(trx);
	}

	/* Unknown is also treated like a record lock */
	if (lock_type == ULINT_UNDEFINED || lock_type == LOCK_REC) {
		thd_wait_begin(trx->mysql_thd, THD_WAIT_ROW_LOCK);
	} else {
		ut_ad(lock_type == LOCK_TABLE);
		thd_wait_begin(trx->mysql_thd, THD_WAIT_TABLE_LOCK);
	}

	DEBUG_SYNC_C("lock_wait_before_suspend");
	os_event_wait(slot->event);
	DBUG_EXECUTE_IF("small_sleep_after_lock_wait",
		{
			if (lock_type == LOCK_REC && lock_mode == LOCK_X &&
			    trx->error_state != DB_DEADLOCK &&
			    !trx_is_interrupted(trx)) {
				my_sleep(20000);
			}
		});

	thd_wait_end(trx->mysql_thd);

	/* After resuming, reacquire the data dictionary latch if
	necessary. */

	if (was_declared_inside_innodb) {

		/* Return back inside InnoDB */

		srv_conc_force_enter_innodb(trx);
	}

	if (had_dict_lock) {

		row_mysql_freeze_data_dictionary(trx);
	}

	double wait_time = difftime(time(NULL), slot->suspend_time);

	/* Release the slot for others to use */

	lock_wait_table_release_slot(slot);

	if (thr->lock_state == QUE_THR_LOCK_ROW) {
		const ulonglong finish_time = my_interval_timer();

		if (finish_time >= start_time) {
			const ulint diff_time = static_cast<ulint>
				((finish_time - start_time) / 1000);
			srv_stats.n_lock_wait_time.add(diff_time);
			/* Only update the variable if we successfully
			retrieved the start and finish times. See Bug#36819. */
			if (diff_time > lock_sys.n_lock_max_wait_time) {
				lock_sys.n_lock_max_wait_time = diff_time;
			}
			/* Record the lock wait time for this thread */
			thd_storage_lock_wait(trx->mysql_thd, diff_time);
		}

		srv_stats.n_lock_wait_current_count--;

		DBUG_EXECUTE_IF("lock_instrument_slow_query_log",
			os_thread_sleep(1000););
	}

	/* The transaction is chosen as deadlock victim during sleep. */
	if (trx->error_state == DB_DEADLOCK) {
		return;
	}

	if (lock_wait_timeout < 100000000
	    && wait_time > (double) lock_wait_timeout
#ifdef WITH_WSREP
	    && (!trx->is_wsrep()
		|| (!wsrep_is_BF_lock_timeout(trx)
		    && trx->error_state != DB_DEADLOCK))
#endif /* WITH_WSREP */
	    ) {

		trx->error_state = DB_LOCK_WAIT_TIMEOUT;

		MONITOR_INC(MONITOR_TIMEOUT);
	}

	if (trx_is_interrupted(trx)) {

		trx->error_state = DB_INTERRUPTED;
	}
}

/********************************************************************//**
Releases a user OS thread waiting for a lock to be released, if the
thread is already suspended. */
void
lock_wait_release_thread_if_suspended(
/*==================================*/
	que_thr_t*	thr)	/*!< in: query thread associated with the
				user OS thread	 */
{
	ut_ad(lock_mutex_own());
	ut_ad(trx_mutex_own(thr_get_trx(thr)));

	/* We own both the lock mutex and the trx_t::mutex but not the
	lock wait mutex. This is OK because other threads will see the state
	of this slot as being in use and no other thread can change the state
	of the slot to free unless that thread also owns the lock mutex. */

	if (thr->slot != NULL && thr->slot->in_use && thr->slot->thr == thr) {
		trx_t*	trx = thr_get_trx(thr);

		if (trx->lock.was_chosen_as_deadlock_victim) {

			trx->error_state = DB_DEADLOCK;
			trx->lock.was_chosen_as_deadlock_victim = false;
		}

		os_event_set(thr->slot->event);
	}
}

/*********************************************************************//**
Check if the thread lock wait has timed out. Release its locks if the
wait has actually timed out. */
static
void
lock_wait_check_and_cancel(
/*=======================*/
	const srv_slot_t*	slot)	/*!< in: slot reserved by a user
					thread when the wait started */
{
	ut_ad(lock_wait_mutex_own());
	ut_ad(slot->in_use);
	ut_ad(slot->suspended);

	double wait_time = difftime(time(NULL), slot->suspend_time);
	trx_t* trx = thr_get_trx(slot->thr);

	if (trx_is_interrupted(trx)
	    || (slot->wait_timeout < 100000000
		&& (wait_time > (double) slot->wait_timeout
		   || wait_time < 0))) {

		/* Timeout exceeded or a wrap-around in system
		time counter: cancel the lock request queued
		by the transaction and release possible
		other transactions waiting behind; it is
		possible that the lock has already been
		granted: in that case do nothing */

		lock_mutex_enter();

		trx_mutex_enter(trx);

		if (trx->lock.wait_lock != NULL) {

			ut_a(trx->lock.que_state == TRX_QUE_LOCK_WAIT);

#ifdef WITH_WSREP
                        if (!wsrep_is_BF_lock_timeout(trx)) {
#endif /* WITH_WSREP */
				lock_cancel_waiting_and_release(trx->lock.wait_lock);
#ifdef WITH_WSREP
                        }
#endif /* WITH_WSREP */
		}

		lock_mutex_exit();

		trx_mutex_exit(trx);
	}
}

/*********************************************************************//**
A thread which wakes up threads whose lock wait may have lasted too long.
@return a dummy parameter */
extern "C"
os_thread_ret_t
DECLARE_THREAD(lock_wait_timeout_thread)(void*)
{
	int64_t		sig_count = 0;
	os_event_t	event = lock_sys.timeout_event;

	ut_ad(!srv_read_only_mode);

#ifdef UNIV_PFS_THREAD
	pfs_register_thread(srv_lock_timeout_thread_key);
#endif /* UNIV_PFS_THREAD */

	do {
		srv_slot_t*	slot;

		/* When someone is waiting for a lock, we wake up every second
		and check if a timeout has passed for a lock wait */

		os_event_wait_time_low(event, 1000000, sig_count);
		sig_count = os_event_reset(event);

		if (srv_shutdown_state >= SRV_SHUTDOWN_CLEANUP) {
			break;
		}

		lock_wait_mutex_enter();

		/* Check all slots for user threads that are waiting
	       	on locks, and if they have exceeded the time limit. */

		for (slot = lock_sys.waiting_threads;
		     slot < lock_sys.last_slot;
		     ++slot) {

			/* We are doing a read without the lock mutex
			and/or the trx mutex. This is OK because a slot
		       	can't be freed or reserved without the lock wait
		       	mutex. */

			if (slot->in_use) {
				lock_wait_check_and_cancel(slot);
			}
		}

		sig_count = os_event_reset(event);

		lock_wait_mutex_exit();

	} while (srv_shutdown_state < SRV_SHUTDOWN_CLEANUP);

	lock_sys.timeout_thread_active = false;

	/* We count the number of threads in os_thread_exit(). A created
	thread should always use that to exit and not use return() to exit. */

	os_thread_exit();

	OS_THREAD_DUMMY_RETURN;
}


/*****************************************************************************

Copyright (c) 1995, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2017, 2022, MariaDB Corporation.

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
@file include/os0proc.h
The interface to the operating system
process control primitives

Created 9/30/1995 Heikki Tuuri
*******************************************************/

#ifndef os0proc_h
#define os0proc_h

#include "univ.i"

#ifdef __linux__
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

typedef void*			os_process_t;
typedef unsigned long int	os_process_id_t;

/** The total amount of memory currently allocated from the operating
system with os_mem_alloc_large(). */
extern Atomic_counter<ulint>	os_total_large_mem_allocated;

/** Converts the current process id to a number.
@return process id as a number */
ulint
os_proc_get_number(void);

/** Allocates large pages memory.
@param[in,out]	n	Number of bytes to allocate
@return allocated memory */
void*
os_mem_alloc_large(
	ulint*	n);

/** Frees large pages memory.
@param[in]	ptr	pointer returned by os_mem_alloc_large()
@param[in]	size	size returned by os_mem_alloc_large() */
void
os_mem_free_large(
	void	*ptr,
	ulint	size);

#endif

/*****************************************************************************

Copyright (c) 1995, 2016, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2019, 2022, MariaDB Corporation.

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
@file os/os0proc.cc
The interface to the operating system
process control primitives

Created 9/30/1995 Heikki Tuuri
*******************************************************/

#include "univ.i"
#ifdef HAVE_LINUX_LARGE_PAGES
# include "mysqld.h"
#endif
#include "my_valgrind.h"

/* FreeBSD for example has only MAP_ANON, Linux has MAP_ANONYMOUS and
MAP_ANON but MAP_ANON is marked as deprecated */
#if defined(MAP_ANONYMOUS)
#define OS_MAP_ANON	MAP_ANONYMOUS
#elif defined(MAP_ANON)
#define OS_MAP_ANON	MAP_ANON
#endif

/** The total amount of memory currently allocated from the operating
system with os_mem_alloc_large(). */
Atomic_counter<ulint>	os_total_large_mem_allocated;

/** Converts the current process id to a number.
@return process id as a number */
ulint
os_proc_get_number(void)
/*====================*/
{
#ifdef _WIN32
	return(static_cast<ulint>(GetCurrentProcessId()));
#else
	return(static_cast<ulint>(getpid()));
#endif
}

/** Allocates large pages memory.
@param[in,out]	n	Number of bytes to allocate
@return allocated memory */
void*
os_mem_alloc_large(
	ulint*	n)
{
	void*	ptr;
	ulint	size;
#ifdef HAVE_LINUX_LARGE_PAGES
	int shmid;
	struct shmid_ds buf;

	if (!my_use_large_pages || !opt_large_page_size) {
		goto skip;
	}

	/* Align block size to opt_large_page_size */
	ut_ad(ut_is_2pow(opt_large_page_size));
	size = ut_2pow_round(*n + opt_large_page_size - 1,
			     ulint(opt_large_page_size));

	shmid = shmget(IPC_PRIVATE, (size_t) size, SHM_HUGETLB | SHM_R | SHM_W);
	if (shmid < 0) {
		ib::warn() << "Failed to allocate " << size
			<< " bytes. errno " << errno;
		ptr = NULL;
	} else {
		ptr = shmat(shmid, NULL, 0);
		if (ptr == (void*)-1) {
			ib::warn() << "Failed to attach shared memory segment,"
				" errno " << errno;
			ptr = NULL;
		}

		/* Remove the shared memory segment so that it will be
		automatically freed after memory is detached or
		process exits */
		shmctl(shmid, IPC_RMID, &buf);
	}

	if (ptr) {
		*n = size;
		os_total_large_mem_allocated += size;
		MEM_UNDEFINED(ptr, size);
		return(ptr);
	}

	ib::warn() << "Using conventional memory pool";
skip:
#endif /* HAVE_LINUX_LARGE_PAGES */

#ifdef _WIN32
	SYSTEM_INFO	system_info;
	GetSystemInfo(&system_info);

	/* Align block size to system page size */
	ut_ad(ut_is_2pow(system_info.dwPageSize));
	size = *n = ut_2pow_round<ulint>(*n + (system_info.dwPageSize - 1),
					 system_info.dwPageSize);
	ptr = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE,
			   PAGE_READWRITE);
	if (!ptr) {
		ib::info() << "VirtualAlloc(" << size << " bytes) failed;"
			" Windows error " << GetLastError();
	} else {
		os_total_large_mem_allocated += size;
		MEM_UNDEFINED(ptr, size);
	}
#else
	size = getpagesize();
	/* Align block size to system page size */
	ut_ad(ut_is_2pow(size));
	size = *n = ut_2pow_round(*n + (size - 1), size);
	ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | OS_MAP_ANON, -1, 0);
	if (UNIV_UNLIKELY(ptr == (void*) -1)) {
		ib::error() << "mmap(" << size << " bytes) failed;"
			" errno " << errno;
		ptr = NULL;
	} else {
		os_total_large_mem_allocated += size;
		MEM_UNDEFINED(ptr, size);
	}
#endif
	return(ptr);
}

/** Frees large pages memory.
@param[in]	ptr	pointer returned by os_mem_alloc_large()
@param[in]	size	size returned by os_mem_alloc_large() */
void
os_mem_free_large(
	void	*ptr,
	ulint	size)
{
	ut_a(os_total_large_mem_allocated >= size);

#ifdef __SANITIZE_ADDRESS__
	// We could have manually poisoned that memory for ASAN.
	// And we must unpoison it by ourself as specified in documentation
	// for __asan_poison_memory_region() in sanitizer/asan_interface.h
	// munmap() doesn't do it for us automatically.
	MEM_MAKE_ADDRESSABLE(ptr, size);
#endif /* __SANITIZE_ADDRESS__ */

#ifdef HAVE_LINUX_LARGE_PAGES
	if (my_use_large_pages && opt_large_page_size && !shmdt(ptr)) {
		os_total_large_mem_allocated -= size;
		return;
	}
#endif /* HAVE_LINUX_LARGE_PAGES */
#ifdef _WIN32
	/* When RELEASE memory, the size parameter must be 0.
	Do not use MEM_RELEASE with MEM_DECOMMIT. */
	if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
		ib::error() << "VirtualFree(" << ptr << ", " << size
			<< ") failed; Windows error " << GetLastError();
	} else {
		os_total_large_mem_allocated -= size;
	}
#elif !defined OS_MAP_ANON
	ut_free(ptr);
#else
# if defined(__sun__)
	if (munmap(static_cast<caddr_t>(ptr), size)) {
# else
	if (munmap(ptr, size)) {
# endif /* __sun__ */
		ib::error() << "munmap(" << ptr << ", " << size << ") failed;"
			" errno " << errno;
	} else {
		os_total_large_mem_allocated -= size;
	}
#endif
}

/*****************************************************************************

Copyright (c) 1994, 2016, Oracle and/or its affiliates. All Rights Reserved.
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

/********************************************************************//**
@file data/data0data.cc
SQL data field and tuple

Created 5/30/1994 Heikki Tuuri
*************************************************************************/

#include "data0data.h"
#include "rem0rec.h"
#include "rem0cmp.h"
#include "page0page.h"
#include "page0zip.h"
#include "dict0dict.h"
#include "btr0cur.h"
#include "row0upd.h"

#ifdef UNIV_DEBUG
/** Dummy variable to catch access to uninitialized fields.  In the
debug version, dtuple_create() will make all fields of dtuple_t point
to data_error. */
ut_d(byte data_error);
#endif /* UNIV_DEBUG */

/** Trim the tail of an index tuple before insert or update.
After instant ADD COLUMN, if the last fields of a clustered index tuple
match the default values that were explicitly specified or implied during
ADD COLUMN, there will be no need to store them.
NOTE: A page latch in the index must be held, so that the index
may not lose 'instantness' before the trimmed tuple has been
inserted or updated.
@param[in]	index	index possibly with instantly added columns */
void dtuple_t::trim(const dict_index_t& index)
{
	ut_ad(n_fields >= index.n_core_fields);
	ut_ad(n_fields <= index.n_fields);
	ut_ad(index.is_instant());

	ulint i = n_fields;
	for (; i > index.n_core_fields; i--) {
		const dfield_t* dfield = dtuple_get_nth_field(this, i - 1);
		const dict_col_t* col = dict_index_get_nth_col(&index, i - 1);

		if (col->is_dropped()) {
			continue;
		}

		ut_ad(col->is_added());
		ulint len = dfield_get_len(dfield);
		if (len != col->def_val.len) {
			break;
		}

		if (len != 0 && len != UNIV_SQL_NULL
		    && dfield->data != col->def_val.data
		    && memcmp(dfield->data, col->def_val.data, len)) {
			break;
		}
	}

	n_fields = i;
}

/** Compare two data tuples.
@param[in] tuple1 first data tuple
@param[in] tuple2 second data tuple
@return positive, 0, negative if tuple1 is greater, equal, less, than tuple2,
respectively */
int
dtuple_coll_cmp(
	const dtuple_t*	tuple1,
	const dtuple_t*	tuple2)
{
	ulint	n_fields;
	ulint	i;
	int	cmp;

	ut_ad(tuple1 != NULL);
	ut_ad(tuple2 != NULL);
	ut_ad(tuple1->magic_n == DATA_TUPLE_MAGIC_N);
	ut_ad(tuple2->magic_n == DATA_TUPLE_MAGIC_N);
	ut_ad(dtuple_check_typed(tuple1));
	ut_ad(dtuple_check_typed(tuple2));

	n_fields = dtuple_get_n_fields(tuple1);

	cmp = (int) n_fields - (int) dtuple_get_n_fields(tuple2);

	for (i = 0; cmp == 0 && i < n_fields; i++) {
		const dfield_t*	field1	= dtuple_get_nth_field(tuple1, i);
		const dfield_t*	field2	= dtuple_get_nth_field(tuple2, i);
		cmp = cmp_dfield_dfield(field1, field2);
	}

	return(cmp);
}

/*********************************************************************//**
Sets number of fields used in a tuple. Normally this is set in
dtuple_create, but if you want later to set it smaller, you can use this. */
void
dtuple_set_n_fields(
/*================*/
	dtuple_t*	tuple,		/*!< in: tuple */
	ulint		n_fields)	/*!< in: number of fields */
{
	tuple->n_fields = n_fields;
	tuple->n_fields_cmp = n_fields;
}

/**********************************************************//**
Checks that a data field is typed.
@return TRUE if ok */
static
ibool
dfield_check_typed_no_assert(
/*=========================*/
	const dfield_t*	field)	/*!< in: data field */
{
	if (dfield_get_type(field)->mtype > DATA_MTYPE_CURRENT_MAX
	    || dfield_get_type(field)->mtype < DATA_MTYPE_CURRENT_MIN) {

		ib::error() << "Data field type "
			<< dfield_get_type(field)->mtype
			<< ", len " << dfield_get_len(field);

		return(FALSE);
	}

	return(TRUE);
}

/**********************************************************//**
Checks that a data tuple is typed.
@return TRUE if ok */
static
ibool
dtuple_check_typed_no_assert(
/*=========================*/
	const dtuple_t*	tuple)	/*!< in: tuple */
{
	const dfield_t*	field;
	ulint		i;

	if (dtuple_get_n_fields(tuple) > REC_MAX_N_FIELDS) {
		ib::error() << "Index entry has "
			<< dtuple_get_n_fields(tuple) << " fields";
dump:
		fputs("InnoDB: Tuple contents: ", stderr);
		dtuple_print(stderr, tuple);
		putc('\n', stderr);

		return(FALSE);
	}

	for (i = 0; i < dtuple_get_n_fields(tuple); i++) {

		field = dtuple_get_nth_field(tuple, i);

		if (!dfield_check_typed_no_assert(field)) {
			goto dump;
		}
	}

	return(TRUE);
}

#ifdef UNIV_DEBUG
/**********************************************************//**
Checks that a data field is typed. Asserts an error if not.
@return TRUE if ok */
ibool
dfield_check_typed(
/*===============*/
	const dfield_t*	field)	/*!< in: data field */
{
	if (dfield_get_type(field)->mtype > DATA_MTYPE_CURRENT_MAX
	    || dfield_get_type(field)->mtype < DATA_MTYPE_CURRENT_MIN) {

		ib::fatal() << "Data field type "
			<< dfield_get_type(field)->mtype
			<< ", len " << dfield_get_len(field);
	}

	return(TRUE);
}

/**********************************************************//**
Checks that a data tuple is typed. Asserts an error if not.
@return TRUE if ok */
ibool
dtuple_check_typed(
/*===============*/
	const dtuple_t*	tuple)	/*!< in: tuple */
{
	const dfield_t*	field;
	ulint		i;

	for (i = 0; i < dtuple_get_n_fields(tuple); i++) {

		field = dtuple_get_nth_field(tuple, i);

		ut_a(dfield_check_typed(field));
	}

	return(TRUE);
}

/**********************************************************//**
Validates the consistency of a tuple which must be complete, i.e,
all fields must have been set.
@return TRUE if ok */
ibool
dtuple_validate(
/*============*/
	const dtuple_t*	tuple)	/*!< in: tuple */
{
	ut_ad(tuple->magic_n == DATA_TUPLE_MAGIC_N);
#ifdef HAVE_valgrind
	const ulint n_fields = dtuple_get_n_fields(tuple);

	for (ulint i = 0; i < n_fields; i++) {
		const dfield_t*	field = dtuple_get_nth_field(tuple, i);

		if (!dfield_is_null(field)) {
			MEM_CHECK_DEFINED(dfield_get_data(field),
					  dfield_get_len(field));
		}
	}
#endif /* HAVE_valgrind */
	ut_ad(dtuple_check_typed(tuple));

	return(TRUE);
}
#endif /* UNIV_DEBUG */

/*************************************************************//**
Pretty prints a dfield value according to its data type. */
void
dfield_print(
/*=========*/
	const dfield_t*	dfield)	/*!< in: dfield */
{
	const byte*	data;
	ulint		len;
	ulint		i;

	len = dfield_get_len(dfield);
	data = static_cast<const byte*>(dfield_get_data(dfield));

	if (dfield_is_null(dfield)) {
		fputs("NULL", stderr);

		return;
	}

	switch (dtype_get_mtype(dfield_get_type(dfield))) {
	case DATA_CHAR:
	case DATA_VARCHAR:
		for (i = 0; i < len; i++) {
			int	c = *data++;
			putc(isprint(c) ? c : ' ', stderr);
		}

		if (dfield_is_ext(dfield)) {
			fputs("(external)", stderr);
		}
		break;
	case DATA_INT:
		ut_a(len == 4); /* only works for 32-bit integers */
		fprintf(stderr, "%d", (int) mach_read_from_4(data));
		break;
	default:
		ut_error;
	}
}

/*************************************************************//**
Pretty prints a dfield value according to its data type. Also the hex string
is printed if a string contains non-printable characters. */
void
dfield_print_also_hex(
/*==================*/
	const dfield_t*	dfield)	/*!< in: dfield */
{
	const byte*	data;
	ulint		len;
	ulint		prtype;
	ulint		i;
	ibool		print_also_hex;

	len = dfield_get_len(dfield);
	data = static_cast<const byte*>(dfield_get_data(dfield));

	if (dfield_is_null(dfield)) {
		fputs("NULL", stderr);

		return;
	}

	prtype = dtype_get_prtype(dfield_get_type(dfield));

	switch (dtype_get_mtype(dfield_get_type(dfield))) {
		ib_id_t	id;
	case DATA_INT:
		switch (len) {
			ulint	val;
		case 1:
			val = mach_read_from_1(data);

			if (!(prtype & DATA_UNSIGNED)) {
				val &= ~0x80U;
				fprintf(stderr, "%ld", (long) val);
			} else {
				fprintf(stderr, "%lu", (ulong) val);
			}
			break;

		case 2:
			val = mach_read_from_2(data);

			if (!(prtype & DATA_UNSIGNED)) {
				val &= ~0x8000U;
				fprintf(stderr, "%ld", (long) val);
			} else {
				fprintf(stderr, "%lu", (ulong) val);
			}
			break;

		case 3:
			val = mach_read_from_3(data);

			if (!(prtype & DATA_UNSIGNED)) {
				val &= ~0x800000U;
				fprintf(stderr, "%ld", (long) val);
			} else {
				fprintf(stderr, "%lu", (ulong) val);
			}
			break;

		case 4:
			val = mach_read_from_4(data);

			if (!(prtype & DATA_UNSIGNED)) {
				val &= ~0x80000000;
				fprintf(stderr, "%ld", (long) val);
			} else {
				fprintf(stderr, "%lu", (ulong) val);
			}
			break;

		case 6:
			id = mach_read_from_6(data);
			fprintf(stderr, IB_ID_FMT, id);
			break;

		case 7:
			id = mach_read_from_7(data);
			fprintf(stderr, IB_ID_FMT, id);
			break;
		case 8:
			id = mach_read_from_8(data);
			fprintf(stderr, IB_ID_FMT, id);
			break;
		default:
			goto print_hex;
		}
		break;

	case DATA_SYS:
		switch (prtype & DATA_SYS_PRTYPE_MASK) {
		case DATA_TRX_ID:
			id = mach_read_from_6(data);

			fprintf(stderr, "trx_id " TRX_ID_FMT, id);
			break;

		case DATA_ROLL_PTR:
			id = mach_read_from_7(data);

			fprintf(stderr, "roll_ptr " TRX_ID_FMT, id);
			break;

		case DATA_ROW_ID:
			id = mach_read_from_6(data);

			fprintf(stderr, "row_id " TRX_ID_FMT, id);
			break;

		default:
			goto print_hex;
		}
		break;

	case DATA_CHAR:
	case DATA_VARCHAR:
		print_also_hex = FALSE;

		for (i = 0; i < len; i++) {
			int c = *data++;

			if (!isprint(c)) {
				print_also_hex = TRUE;

				fprintf(stderr, "\\x%02x", (unsigned char) c);
			} else {
				putc(c, stderr);
			}
		}

		if (dfield_is_ext(dfield)) {
			fputs("(external)", stderr);
		}

		if (!print_also_hex) {
			break;
		}

		data = static_cast<const byte*>(dfield_get_data(dfield));
		/* fall through */

	case DATA_BINARY:
	default:
print_hex:
		fputs(" Hex: ",stderr);

		for (i = 0; i < len; i++) {
			fprintf(stderr, "%02x", *data++);
		}

		if (dfield_is_ext(dfield)) {
			fputs("(external)", stderr);
		}
	}
}

/*************************************************************//**
Print a dfield value using ut_print_buf. */
static
void
dfield_print_raw(
/*=============*/
	FILE*		f,		/*!< in: output stream */
	const dfield_t*	dfield)		/*!< in: dfield */
{
	ulint	len	= dfield_get_len(dfield);
	if (!dfield_is_null(dfield)) {
		ulint	print_len = ut_min(len, static_cast<ulint>(1000));
		ut_print_buf(f, dfield_get_data(dfield), print_len);
		if (len != print_len) {
			fprintf(f, "(total %lu bytes%s)",
				(ulong) len,
				dfield_is_ext(dfield) ? ", external" : "");
		}
	} else {
		fputs(" SQL NULL", f);
	}
}

/**********************************************************//**
The following function prints the contents of a tuple. */
void
dtuple_print(
/*=========*/
	FILE*		f,	/*!< in: output stream */
	const dtuple_t*	tuple)	/*!< in: tuple */
{
	ulint		n_fields;
	ulint		i;

	n_fields = dtuple_get_n_fields(tuple);

	fprintf(f, "DATA TUPLE: %lu fields;\n", (ulong) n_fields);

	for (i = 0; i < n_fields; i++) {
		fprintf(f, " %lu:", (ulong) i);

		dfield_print_raw(f, dtuple_get_nth_field(tuple, i));

		putc(';', f);
		putc('\n', f);
	}

	ut_ad(dtuple_validate(tuple));
}

/** Print the contents of a tuple.
@param[out]	o	output stream
@param[in]	field	array of data fields
@param[in]	n	number of data fields */
void
dfield_print(
	std::ostream&	o,
	const dfield_t*	field,
	ulint		n)
{
	for (ulint i = 0; i < n; i++, field++) {
		const void*	data	= dfield_get_data(field);
		const ulint	len	= dfield_get_len(field);

		if (i) {
			o << ',';
		}

		if (dfield_is_null(field)) {
			o << "NULL";
		} else if (dfield_is_ext(field)) {
			ulint	local_len = len - BTR_EXTERN_FIELD_REF_SIZE;
			ut_ad(len >= BTR_EXTERN_FIELD_REF_SIZE);

			o << '['
			  << local_len
			  << '+' << BTR_EXTERN_FIELD_REF_SIZE << ']';
			ut_print_buf(o, data, local_len);
			ut_print_buf_hex(o, static_cast<const byte*>(data)
					 + local_len,
					 BTR_EXTERN_FIELD_REF_SIZE);
		} else {
			o << '[' << len << ']';
			ut_print_buf(o, data, len);
		}
	}
}

/** Print the contents of a tuple.
@param[out]	o	output stream
@param[in]	tuple	data tuple */
void
dtuple_print(
	std::ostream&	o,
	const dtuple_t*	tuple)
{
	const ulint	n	= dtuple_get_n_fields(tuple);

	o << "TUPLE (info_bits=" << dtuple_get_info_bits(tuple)
	  << ", " << n << " fields): {";

	dfield_print(o, tuple->fields, n);

	o << "}";
}

/**************************************************************//**
Moves parts of long fields in entry to the big record vector so that
the size of tuple drops below the maximum record size allowed in the
database. Moves data only from those fields which are not necessary
to determine uniquely the insertion place of the tuple in the index.
@return own: created big record vector, NULL if we are not able to
shorten the entry enough, i.e., if there are too many fixed-length or
short fields in entry or the index is clustered */
big_rec_t*
dtuple_convert_big_rec(
/*===================*/
	dict_index_t*	index,	/*!< in: index */
	upd_t*		upd,	/*!< in/out: update vector */
	dtuple_t*	entry,	/*!< in/out: index entry */
	ulint*		n_ext)	/*!< in/out: number of
				externally stored columns */
{
	mem_heap_t*	heap;
	big_rec_t*	vector;
	dfield_t*	dfield;
	ulint		size;
	ulint		local_prefix_len;

	if (!dict_index_is_clust(index)) {
		return(NULL);
	}

	if (!index->table->space) {
		return NULL;
	}

	ulint local_len = index->table->get_overflow_field_local_len();
	const auto zip_size = index->table->space->zip_size();

	ut_ad(index->n_uniq > 0);

	ut_a(dtuple_check_typed_no_assert(entry));

	size = rec_get_converted_size(index, entry, *n_ext);

	if (UNIV_UNLIKELY(size > 1000000000)) {
		ib::warn() << "Tuple size is very big: " << size;
		fputs("InnoDB: Tuple contents: ", stderr);
		dtuple_print(stderr, entry);
		putc('\n', stderr);
	}

	heap = mem_heap_create(size + dtuple_get_n_fields(entry)
			       * sizeof(big_rec_field_t) + 1000);

	vector = big_rec_t::alloc(heap, dtuple_get_n_fields(entry));

	/* Decide which fields to shorten: the algorithm is to look for
	a variable-length field that yields the biggest savings when
	stored externally */

	ut_d(ulint n_fields = 0);
	ulint longest_i;

	const bool mblob = entry->is_alter_metadata();
	ut_ad(entry->n_fields >= index->first_user_field() + mblob);
	ut_ad(entry->n_fields - mblob <= index->n_fields);

	if (mblob) {
		longest_i = index->first_user_field();
		dfield = dtuple_get_nth_field(entry, longest_i);
		local_len = BTR_EXTERN_FIELD_REF_SIZE;
		ut_ad(!dfield_is_ext(dfield));
		goto ext_write;
	}

	if (!dict_table_has_atomic_blobs(index->table)) {
		/* up to MySQL 5.1: store a 768-byte prefix locally */
		local_len = BTR_EXTERN_FIELD_REF_SIZE
			+ DICT_ANTELOPE_MAX_INDEX_COL_LEN;
	} else {
		/* new-format table: do not store any BLOB prefix locally */
		local_len = BTR_EXTERN_FIELD_REF_SIZE;
	}

	while (page_zip_rec_needs_ext(rec_get_converted_size(index, entry,
							     *n_ext),
				      index->table->not_redundant(),
				      dict_index_get_n_fields(index),
				      zip_size)) {
		longest_i = 0;
		for (ulint i = index->first_user_field(), longest = 0;
		     i + mblob < entry->n_fields; i++) {
			ulint	savings;
			dfield = dtuple_get_nth_field(entry, i + mblob);

			const dict_field_t* ifield = dict_index_get_nth_field(
				index, i);

			/* Skip fixed-length, NULL, externally stored,
			or short columns */

			if (ifield->fixed_len
			    || dfield_is_null(dfield)
			    || dfield_is_ext(dfield)
			    || dfield_get_len(dfield) <= local_len
			    || dfield_get_len(dfield)
			    <= BTR_EXTERN_LOCAL_STORED_MAX_SIZE) {
				goto skip_field;
			}

			savings = dfield_get_len(dfield) - local_len;

			/* Check that there would be savings */
			if (longest >= savings) {
				goto skip_field;
			}

			/* In DYNAMIC and COMPRESSED format, store
			locally any non-BLOB columns whose maximum
			length does not exceed 256 bytes.  This is
			because there is no room for the "external
			storage" flag when the maximum length is 255
			bytes or less. This restriction trivially
			holds in REDUNDANT and COMPACT format, because
			there we always store locally columns whose
			length is up to local_len == 788 bytes.
			@see rec_init_offsets_comp_ordinary */
			if (!DATA_BIG_COL(ifield->col)) {
				goto skip_field;
			}

			longest_i = i + mblob;
			longest = savings;

skip_field:
			continue;
		}

		if (!longest_i) {
			/* Cannot shorten more */

			mem_heap_free(heap);

			return(NULL);
		}

		/* Move data from field longest_i to big rec vector.

		We store the first bytes locally to the record. Then
		we can calculate all ordering fields in all indexes
		from locally stored data. */
		dfield = dtuple_get_nth_field(entry, longest_i);
ext_write:
		local_prefix_len = local_len - BTR_EXTERN_FIELD_REF_SIZE;

		vector->append(
			big_rec_field_t(
				longest_i,
				dfield_get_len(dfield) - local_prefix_len,
				static_cast<char*>(dfield_get_data(dfield))
				+ local_prefix_len));

		/* Allocate the locally stored part of the column. */
		byte* data = static_cast<byte*>(
			mem_heap_alloc(heap, local_len));

		/* Copy the local prefix. */
		memcpy(data, dfield_get_data(dfield), local_prefix_len);
		/* Clear the extern field reference (BLOB pointer). */
		memset(data + local_prefix_len, 0, BTR_EXTERN_FIELD_REF_SIZE);

		dfield_set_data(dfield, data, local_len);
		dfield_set_ext(dfield);

		(*n_ext)++;
		ut_ad(++n_fields < dtuple_get_n_fields(entry));

		if (upd && !upd->is_modified(longest_i)) {

			DEBUG_SYNC_C("ib_mv_nonupdated_column_offpage");

			upd_field_t	upd_field;
			upd_field.field_no = unsigned(longest_i);
			upd_field.orig_len = 0;
			upd_field.exp = NULL;
			upd_field.old_v_val = NULL;
			dfield_copy(&upd_field.new_val,
				    dfield->clone(upd->heap));
			upd->append(upd_field);
			ut_ad(upd->is_modified(longest_i));

			ut_ad(upd_field.new_val.len
			      >= BTR_EXTERN_FIELD_REF_SIZE);
			ut_ad(upd_field.new_val.len == local_len);
			ut_ad(upd_field.new_val.len == dfield_get_len(dfield));
		}
	}

	ut_ad(n_fields == vector->n_fields);

	return(vector);
}

/**************************************************************//**
Puts back to entry the data stored in vector. Note that to ensure the
fields in entry can accommodate the data, vector must have been created
from entry with dtuple_convert_big_rec. */
void
dtuple_convert_back_big_rec(
/*========================*/
	dict_index_t*	index MY_ATTRIBUTE((unused)),	/*!< in: index */
	dtuple_t*	entry,	/*!< in/out: entry whose data was put to vector */
	big_rec_t*	vector)	/*!< in, own: big rec vector; it is
				freed in this function */
{
	big_rec_field_t*		b	= vector->fields;
	const big_rec_field_t* const	end	= b + vector->n_fields;

	for (; b < end; b++) {
		dfield_t*	dfield;
		ulint		local_len;

		dfield = dtuple_get_nth_field(entry, b->field_no);
		local_len = dfield_get_len(dfield);

		ut_ad(dfield_is_ext(dfield));
		ut_ad(local_len >= BTR_EXTERN_FIELD_REF_SIZE);

		local_len -= BTR_EXTERN_FIELD_REF_SIZE;

		/* Only in REDUNDANT and COMPACT format, we store
		up to DICT_ANTELOPE_MAX_INDEX_COL_LEN (768) bytes
		locally */
		ut_ad(local_len <= DICT_ANTELOPE_MAX_INDEX_COL_LEN);

		dfield_set_data(dfield,
				(char*) b->data - local_len,
				b->len + local_len);
	}

	mem_heap_free(vector->heap);
}

/** Allocate a big_rec_t object in the given memory heap, and for storing
n_fld number of fields.
@param[in]	heap	memory heap in which this object is allocated
@param[in]	n_fld	maximum number of fields that can be stored in
			this object

@return the allocated object */
big_rec_t*
big_rec_t::alloc(
	mem_heap_t*	heap,
	ulint		n_fld)
{
	big_rec_t*	rec = static_cast<big_rec_t*>(
		mem_heap_alloc(heap, sizeof(big_rec_t)));

	new(rec) big_rec_t(n_fld);

	rec->heap = heap;
	rec->fields = static_cast<big_rec_field_t*>(
		mem_heap_alloc(heap,
			       n_fld * sizeof(big_rec_field_t)));

	rec->n_fields = 0;
	return(rec);
}

/** Create a deep copy of this object.
@param[in,out]	heap	memory heap in which the clone will be created
@return	the cloned object */
dfield_t*
dfield_t::clone(mem_heap_t* heap) const
{
	const ulint size = len == UNIV_SQL_NULL ? 0 : len;
	dfield_t* obj = static_cast<dfield_t*>(
		mem_heap_alloc(heap, sizeof(dfield_t) + size));

	ut_ad(len != UNIV_SQL_DEFAULT);
	obj->ext  = ext;
	obj->len  = len;
	obj->type = type;
	obj->spatial_status = spatial_status;

	if (len != UNIV_SQL_NULL) {
		obj->data = obj + 1;
		memcpy(obj->data, data, len);
	} else {
		obj->data = 0;
	}

	return(obj);
}

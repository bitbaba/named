/*
 * Copyright (C) 1996, 1997, 1998, 1999  Internet Software Consortium.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/* $Id: generic.c,v 1.6 2000/01/13 06:13:22 tale Exp $ */

/* Principal Author: Ted Lemon */

/*
 * Subroutines that support the generic object.
 */
#include <stddef.h>		/* NULL */
#include <string.h>		/* memset */

#include <isc/assertions.h>

#include <omapi/private.h>

isc_result_t
omapi_generic_new(omapi_object_t **gen, const char *name) {
	omapi_generic_object_t *obj;

	obj = isc_mem_get(omapi_mctx, sizeof(*obj));
	if (obj == NULL)
		return (ISC_R_NOMEMORY);
	memset(obj, 0, sizeof(*obj));
	obj->refcnt = 0;
	obj->type = omapi_type_generic;

	OBJECT_REF(gen, obj, name);

	return (ISC_R_SUCCESS);
}

isc_result_t
omapi_generic_set_value(omapi_object_t *h, omapi_object_t *id,
			omapi_data_string_t *name, omapi_typed_data_t *value)
{
	omapi_generic_object_t *g;
	omapi_value_t *new;
	omapi_value_t **va;
	int vm_new;
	unsigned int i;
	isc_result_t result;

	REQUIRE(h != NULL && h->type == omapi_type_generic);

	g = (omapi_generic_object_t *)h;

	/*
	 * See if there's already a value with this name attached to
	 * the generic object, and if so, replace the current value
	 * with the new one.
	 */
	for (i = 0; i < g->nvalues; i++) {
		if (omapi_data_string_cmp(name, g->values[i]->name) == 0) {
			/*
			 * There's an inconsistency here: the standard
			 * behaviour of a set_values method when
			 * passed a matching name and a null value is
			 * to delete the value associated with that
			 * name (where possible).  In the generic
			 * object, we remember the name/null pair,
			 * because generic objects are generally used
			 * to pass messages around, and this is the
			 * way that remote entities delete values from
			 * local objects.  If the get_value method of
			 * a generic object is called for a name that
			 * maps to a name/null pair, ISC_R_NOTFOUND is
			 * returned.
			 */
			new = NULL;
			result = omapi_data_newvalue(&new,
						    "omapi_message_get_value");
			if (result != ISC_R_SUCCESS)
				return (result);

			omapi_data_stringreference(&new->name, name,
						    "omapi_message_get_value");
			if (value != NULL)
				omapi_data_reference(&new->value, value,
						    "omapi_generic_set_value");

			omapi_data_valuedereference(&(g->values[i]),
						"omapi_message_set_value");
			omapi_data_valuereference(&(g->values[i]), new,
					        "omapi_message_set_value");
			omapi_data_valuedereference(&new,
						"omapi_message_set_value");

			return (ISC_R_SUCCESS);
		}
	}			

	/*
	 * If the name isn't already attached to this object, see if an
	 * inner object has it.
	 */
	if (h->inner != NULL && h->inner->type->set_value != NULL) {
		result = (*(h->inner->type->set_value))(h->inner, id,
							name, value);
		if (result != ISC_R_NOTFOUND)
			return (result);
	}

	/*
	 * Okay, so it's a value that no inner object knows about, and
	 * (implicitly, since the outer object set_value method would
	 * have called this object's set_value method) it's an object that
	 * no outer object knows about, it's this object's responsibility
	 * to remember it - that's what generic objects do.
	 */

	/*
	 * Arrange for there to be space for the pointer to the new
	 * name/value pair if necessary.
	 */
	if (g->nvalues == g->va_max) {
		/*
		 * Increase the maximum number of values by 10.
		 * 10 is an arbitrary constant.
		 */
		vm_new = g->va_max + 10;
		va = isc_mem_get(omapi_mctx, vm_new * sizeof(*va));
		if (va == NULL)
			return (ISC_R_NOMEMORY);
		if (g->va_max != 0) {
			memcpy(va, g->values, g->va_max * sizeof(*va));
			isc_mem_put(omapi_mctx, g->values,
				    g->va_max * sizeof(*va));
		}

		memset(va + g->va_max, 0, (vm_new - g->va_max) * sizeof(*va));
		g->values = va;
		g->va_max = vm_new;
	}
	result = omapi_data_newvalue(&g->values[g->nvalues],
				     "omapi_generic_set_value");
	if (result != ISC_R_SUCCESS)
		return (result);

	omapi_data_stringreference(&g->values[g->nvalues]->name, name,
				   "omapi_generic_set_value");
	if (value != NULL)
		omapi_data_reference(&g->values[g->nvalues]->value, value,
				     "omapi_generic_set_value");
	g->nvalues++;
	return (ISC_R_SUCCESS);
}

isc_result_t
omapi_generic_get_value(omapi_object_t *h, omapi_object_t *id,
			omapi_data_string_t *name, omapi_value_t **value)
{
	unsigned int i;
	omapi_generic_object_t *g;

	REQUIRE(h != NULL && h->type == omapi_type_generic);

	g = (omapi_generic_object_t *)h;
	
	/*
	 * Look up the specified name in our list of objects.
	 */
	for (i = 0; i < g->nvalues; i++) {
		if (omapi_data_string_cmp(name, g->values[i]->name) == 0) {
			/*
			 * If this is a name/null value pair, this is the
			 * same as if there were no value that matched
			 * the specified name, so return ISC_R_NOTFOUND.
			 */
			if (g->values[i]->value == NULL)
				return (ISC_R_NOTFOUND);
			/*
			 * Otherwise, return the name/value pair.
			 */
			omapi_data_valuereference(value, g->values[i],
						  "omapi_message_get_value");
			return (ISC_R_SUCCESS);
		}
	}			

	PASS_GETVALUE(h);
}

void
omapi_generic_destroy(omapi_object_t *h, const char *name) {
	omapi_generic_object_t *g;
	unsigned int i;

	REQUIRE(h != NULL && h->type == omapi_type_generic);

	g = (omapi_generic_object_t *)h;
	
	if (g->values != NULL) {
		for (i = 0; i < g->nvalues; i++)
			if (g->values[i] != NULL)
				omapi_data_valuedereference(&g->values[i],
							    name);

		isc_mem_put(omapi_mctx, g->values,
			    g->va_max * sizeof(*g->values));
		g->values = NULL;
		g->va_max = 0;
	}
}

isc_result_t
omapi_generic_signal_handler(omapi_object_t *h, const char *name, va_list ap) {

	REQUIRE(h != NULL && h->type == omapi_type_generic);

	PASS_SIGNAL(h);
}

/*
 * Write all the published values associated with the object through the
 * specified connection.
 */

isc_result_t
omapi_generic_stuff_values(omapi_object_t *connection, omapi_object_t *id,
			   omapi_object_t *h)
{
	omapi_generic_object_t *src;
	unsigned int i;
	isc_result_t result;

	REQUIRE(h != NULL && h->type == omapi_type_generic);

	src = (omapi_generic_object_t *)h;
	
	for (i = 0; i < src->nvalues; i++) {
		if (src->values[i] != NULL &&
		    src->values[i]->name->len != 0) {
			result = omapi_connection_putuint16(connection,
						   src->values[i]->name->len);
			if (result != ISC_R_SUCCESS)
				return (result);
			result = omapi_connection_copyin(connection,
						   src->values[i]->name->value,
						   src->values[i]->name->len);
			if (result != ISC_R_SUCCESS)
				return (result);

			result = omapi_connection_puttypeddata(connection,
						       src->values[i]->value);
			if (result != ISC_R_SUCCESS)
				return (result);
		}
	}			

	PASS_STUFFVALUES(h);
}

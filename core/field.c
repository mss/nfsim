/*

Copyright (c) 2004 Rusty Russell

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* FIXME: just want list macros. */
#include <kernelenv.h>
#include <core.h>
#include <talloc.h>
#include <field.h>
#include <utils.h>

struct field
{
	struct list_head list;
	const char *name;
	const void *strct;
	void *val;
};

static LIST_HEAD(fields);
static void *field_ctx;

static int field_destroy(void *f)
{
	struct field *field = f;
	list_del(&field->list);
	return 0;
}

static void __field_attach(void *ctx, const void *strct,
			   const char *name, void *val)
{
	struct field *field = talloc(ctx, struct field);

	field->strct = strct;
	field->name = name;
	field->val = val;
	list_add(&field->list, &fields);
	talloc_set_destructor(field, field_destroy);
	talloc_steal(field, val);
}

/* strct is talloc'ed: field destroyed when strct is. */
void field_attach(const void *strct, const char *name, void *val)
{
	return __field_attach((void *)strct, strct, name, val);
}

/* strct is not talloc'ed: field destroyed manually.  */
void field_attach_static(const void *strct, const char *name, void *val)
{
	return __field_attach(field_ctx, strct, name, val);
}

static struct field *__field_find(const void *strct, const char *name)
{
	struct field *i;

	list_for_each_entry(i, &fields, list) {
		if (strct == i->strct && streq(name, i->name))
			return i;
	}
	return NULL;
}

bool field_exists(const void *strct, const char *name)
{
	return (__field_find(strct, name) != NULL);
}

void *field_value(const void *strct, const char *name)
{
	struct field *f = __field_find(strct, name);

	if (!f)
		return NULL;
	return f->val;
}

void field_detach(const void *strct, const char *name)
{
	talloc_free(__field_find(strct, name));
}

void field_detach_all(const void *strct)
{
	struct field *i, *tmp;

	list_for_each_entry_safe(i, tmp, &fields, list)
		if (i->strct == strct)
			talloc_free(i);
}

static void setup_field(void)
{
	field_ctx = talloc_named_const(nfsim_tallocs, 1, "Extra fields");
}
init_call(setup_field);

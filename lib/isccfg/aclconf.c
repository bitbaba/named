/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: aclconf.c,v 1.10 2007/09/12 01:09:08 each Exp $ */

#include <config.h>

#include <isc/mem.h>
#include <isc/string.h>		/* Required for HP/UX (and others?) */
#include <isc/util.h>

#include <isccfg/namedconf.h>
#include <isccfg/aclconf.h>

#include <dns/acl.h>
#include <dns/iptable.h>
#include <dns/fixedname.h>
#include <dns/log.h>

#define LOOP_MAGIC ISC_MAGIC('L','O','O','P') 

void
cfg_aclconfctx_init(cfg_aclconfctx_t *ctx) {
	ISC_LIST_INIT(ctx->named_acl_cache);
}

void
cfg_aclconfctx_destroy(cfg_aclconfctx_t *ctx) {
     	dns_acl_t *dacl, *next;

	for (dacl = ISC_LIST_HEAD(ctx->named_acl_cache);
	     dacl != NULL;
	     dacl = next)
	{
		next = ISC_LIST_NEXT(dacl, nextincache);
		dns_acl_detach(&dacl);
	}
}

/*
 * Find the definition of the named acl whose name is "name".
 */
static isc_result_t
get_acl_def(const cfg_obj_t *cctx, const char *name, const cfg_obj_t **ret) {
	isc_result_t result;
	const cfg_obj_t *acls = NULL;
	const cfg_listelt_t *elt;
	
	result = cfg_map_get(cctx, "acl", &acls);
	if (result != ISC_R_SUCCESS)
		return (result);
	for (elt = cfg_list_first(acls);
	     elt != NULL;
	     elt = cfg_list_next(elt)) {
		const cfg_obj_t *acl = cfg_listelt_value(elt);
		const char *aclname = cfg_obj_asstring(cfg_tuple_get(acl, "name"));
		if (strcasecmp(aclname, name) == 0) {
			if (ret != NULL) {
				*ret = cfg_tuple_get(acl, "value");
			}
			return (ISC_R_SUCCESS);
		}
	}
	return (ISC_R_NOTFOUND);
}

static isc_result_t
convert_named_acl(const cfg_obj_t *nameobj, const cfg_obj_t *cctx,
		  isc_log_t *lctx, cfg_aclconfctx_t *ctx,
		  isc_mem_t *mctx, int nest_level,
                  dns_acl_t **target)
{
	isc_result_t result;
	const cfg_obj_t *cacl = NULL;
	dns_acl_t *dacl;
	dns_acl_t loop;
	const char *aclname = cfg_obj_asstring(nameobj);

	/* Look for an already-converted version. */
	for (dacl = ISC_LIST_HEAD(ctx->named_acl_cache);
	     dacl != NULL;
	     dacl = ISC_LIST_NEXT(dacl, nextincache))
	{
		if (strcasecmp(aclname, dacl->name) == 0) {
			if (ISC_MAGIC_VALID(dacl, LOOP_MAGIC)) {
				cfg_obj_log(nameobj, lctx, ISC_LOG_ERROR,
					    "acl loop detected: %s", aclname);
				return (ISC_R_FAILURE);
			}
			dns_acl_attach(dacl, target);
			return (ISC_R_SUCCESS);
		}
	}
	/* Not yet converted.  Convert now. */
	result = get_acl_def(cctx, aclname, &cacl);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(nameobj, lctx, ISC_LOG_WARNING,
			    "undefined ACL '%s'", aclname);
		return (result);
	}
	/*
	 * Add a loop detection element.
	 */
	memset(&loop, 0, sizeof(loop));
	ISC_LINK_INIT(&loop, nextincache);
	DE_CONST(aclname, loop.name);
	loop.magic = LOOP_MAGIC;
	ISC_LIST_APPEND(ctx->named_acl_cache, &loop, nextincache);
	result = cfg_acl_fromconfig(cacl, cctx, lctx, ctx, mctx,
                                    nest_level, &dacl);
	ISC_LIST_UNLINK(ctx->named_acl_cache, &loop, nextincache);
	loop.magic = 0;
	loop.name = NULL;
	if (result != ISC_R_SUCCESS)
		return (result);
	dacl->name = isc_mem_strdup(dacl->mctx, aclname);
	if (dacl->name == NULL)
		return (ISC_R_NOMEMORY);
	ISC_LIST_APPEND(ctx->named_acl_cache, dacl, nextincache);
	dns_acl_attach(dacl, target);
	return (ISC_R_SUCCESS);
}

static isc_result_t
convert_keyname(const cfg_obj_t *keyobj, isc_log_t *lctx, isc_mem_t *mctx,
		dns_name_t *dnsname)
{
	isc_result_t result;
	isc_buffer_t buf;
	dns_fixedname_t fixname;
	unsigned int keylen;
	const char *txtname = cfg_obj_asstring(keyobj);

	keylen = strlen(txtname);
	isc_buffer_init(&buf, txtname, keylen);
	isc_buffer_add(&buf, keylen);
	dns_fixedname_init(&fixname);
	result = dns_name_fromtext(dns_fixedname_name(&fixname), &buf,
				   dns_rootname, ISC_FALSE, NULL);
	if (result != ISC_R_SUCCESS) {
		cfg_obj_log(keyobj, lctx, ISC_LOG_WARNING,
			    "key name '%s' is not a valid domain name",
			    txtname);
		return (result);
	}
	return (dns_name_dup(dns_fixedname_name(&fixname), mctx, dnsname));
}

isc_result_t
cfg_acl_fromconfig(const cfg_obj_t *caml,
		   const cfg_obj_t *cctx,
	 	   isc_log_t *lctx,
		   cfg_aclconfctx_t *ctx,
		   isc_mem_t *mctx,
                   int nest_level,
		   dns_acl_t **target)
{
	isc_result_t result;
	dns_acl_t *dacl = NULL, *inneracl = NULL;
	dns_aclelement_t *de;
	const cfg_listelt_t *elt;
        dns_iptable_t *iptab;

	REQUIRE(target != NULL);
        REQUIRE(*target == NULL || ISC_MAGIC_VALID(target, DNS_ACL_MAGIC));

        if (*target != NULL) {
                /*
                 * If target already points to an ACL, then we're being
                 * called recursively to configure a nested ACL.  The
                 * nested ACL's contents should just be absorbed into its
                 * parent ACL.
                 */
                dacl = *target;
        } else {
                /*
                 * Need to allocate a new ACL structure.  Count the items
                 * in the ACL definition and allocate space for that many
                 * elements (even though some or all of them may end up in
                 * the iptable instead of the element array).
                 */
                unsigned int element_count = 0;
                for (elt = cfg_list_first(caml);
                     elt != NULL;
                     elt = cfg_list_next(elt)) {
                        const cfg_obj_t *ce = cfg_listelt_value(elt);
                        if (cfg_obj_istuple(ce))
                                ce = cfg_tuple_get(ce, "value");
                        if (cfg_obj_isnetprefix(ce))
                                element_count++;
                }
        	result = dns_acl_create(mctx, element_count, &dacl);
        	if (result != ISC_R_SUCCESS)
        		return (result);
        }

	de = dacl->elements;
	for (elt = cfg_list_first(caml);
	     elt != NULL;
	     elt = cfg_list_next(elt))
	{
		const cfg_obj_t *ce = cfg_listelt_value(elt);
		isc_boolean_t	neg;

		if (cfg_obj_istuple(ce)) {
			/* This must be a negated element. */
			ce = cfg_tuple_get(ce, "value");
			neg = ISC_TRUE;
		} else
			neg = ISC_FALSE;

                /*
                 * If nest_level is nonzero, then every element is
                 * to be stored as a separate, nested ACL rather than
                 * merged into the main iptable.
                 */
                iptab = dacl->iptable;
        	if (nest_level) {
                        result = dns_acl_create(mctx, 0, &de->nestedacl);
                        if (result != ISC_R_SUCCESS)
		                goto cleanup;
                        iptab = de->nestedacl->iptable;
                }

		if (cfg_obj_isnetprefix(ce)) {
			/* Network prefix */
		        isc_netaddr_t	addr;
		        unsigned int	bitlen;

                        cfg_obj_asnetprefix(ce, &addr, &bitlen);
        		result = dns_iptable_addprefix(iptab, &addr, bitlen,
                                                       ISC_TF(!neg));
			if (result != ISC_R_SUCCESS)
				goto cleanup;
                        continue;
		} else if (cfg_obj_islist(ce)) {
                        /*
                         * If we're nesting ACLs, put the nested
                         * ACL onto the elements list; otherwise
                         * merge it into *this* ACL.
                         */
                        if (nest_level == 0) {
			        result = cfg_acl_fromconfig(ce,
                                                 cctx, lctx, ctx, mctx, 0,
                                                 &dacl);
                        } else {
				de->type = dns_aclelementtype_nestedacl;
		                de->negative = neg;
			        result = cfg_acl_fromconfig(ce,
                                                 cctx, lctx, ctx, mctx,
                                                 nest_level - 1,
                                                 &de->nestedacl);
                        }
			if (result != ISC_R_SUCCESS)
			        goto cleanup;
                        continue;
		} else if (cfg_obj_isstring(ce)) {
			/* ACL name */
			const char *name = cfg_obj_asstring(ce);
                        if (strcasecmp(name, "any") == 0) {
                                /* iptable entry with zero bit length */
                                dns_iptable_addprefix(iptab, NULL, 0,
                                                      ISC_TRUE);
                                continue;
			} else if (strcasecmp(name, "none") == 0) {
                                /* negated "any" */
                                dns_iptable_addprefix(iptab, NULL, 0,
                                                      ISC_FALSE);
                                continue;
			} else if (strcasecmp(name, "localhost") == 0) {
				de->type = dns_aclelementtype_localhost;
		                de->negative = neg;
			} else if (strcasecmp(name, "localnets") == 0) {
				de->type = dns_aclelementtype_localnets;
		                de->negative = neg;
			} else {
				result = get_acl_def(cctx, name, NULL);
				if (result == ISC_R_SUCCESS) {
					/* found it in acl definitions */
                                        inneracl = NULL;
					result = convert_named_acl(ce, cctx,
							lctx, ctx, mctx,
                                                        nest_level
                                                          ?  (nest_level - 1)
                                                          : 0,
                                                        &inneracl);
				}
				if (result != ISC_R_SUCCESS)
					goto cleanup;

                                if (nest_level) {
                                        de->type = dns_aclelementtype_nestedacl,
                                        de->negative = neg;
                                        de->nestedacl = inneracl;
                                } else {
                                        dns_acl_merge(dacl, inneracl,
                                                      ISC_TF(!neg));
                                        dns_acl_detach(&inneracl);
                                }
                                continue;
			}
		} else if (cfg_obj_istype(ce, &cfg_type_keyref)) {
			/* Key name */
			de->type = dns_aclelementtype_keyname;
		        de->negative = neg;
			dns_name_init(&de->keyname, NULL);
			result = convert_keyname(ce, lctx, mctx,
						 &de->keyname);
			if (result != ISC_R_SUCCESS)
				goto cleanup;
		} else {
			cfg_obj_log(ce, lctx, ISC_LOG_WARNING,
				    "address match list contains "
				    "unsupported element type");
			result = ISC_R_FAILURE;
			goto cleanup;
		}

                /*
                 * XXX each: This should only be reached for localhost,
                 * localnets and keyname elements -- probably should
                 * be refactored for clearer flow
                 */
                if (nest_level && de->type != dns_aclelementtype_nestedacl)
                        dns_acl_detach(&de->nestedacl);

                de->node_num = dacl->node_count++;
		de++;
		dacl->length++;
	}

	*target = dacl;
	return (ISC_R_SUCCESS);

 cleanup:
	dns_acl_detach(&dacl);
	return (result);
}

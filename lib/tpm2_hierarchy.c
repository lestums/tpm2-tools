/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <tss2/tss2_esys.h>

#include "log.h"
#include "tpm2_auth_util.h"
#include "tpm2_hierarchy.h"
#include "tpm2_util.h"

/**
 * Parses a hierarchy value from an option argument.
 * @param value
 *  The string to parse, which can be a numerical string as
 *  understood by strtoul() with a base of 0, or an:
 *    - o - Owner hierarchy
 *    - p - Platform hierarchy
 *    - e - Endorsement hierarchy
 *    - n - Null hierarchy
 * @param hierarchy
 *  The parsed hierarchy as output.
 * @param flags
 *  What hierarchies should be supported by
 *  the parsing.
 * @return
 *  True on success, False otherwise.
 */
bool tpm2_hierarchy_from_optarg(const char *value,
        TPMI_RH_PROVISION *hierarchy, tpm2_hierarchy_flags flags) {

    if (!value) {
        return false;
    }

    bool is_o = !strcmp(value, "o");
    if (is_o) {
        if (!(flags & TPM2_HIERARCHY_FLAGS_O)) {
            LOG_ERR("Owner hierarchy not supported by this command.");
            return false;
        }
        *hierarchy = TPM2_RH_OWNER;
        return true;
    }

    bool is_p = !strcmp(value, "p");
    if (is_p) {
        if (!(flags & TPM2_HIERARCHY_FLAGS_P)) {
            LOG_ERR("Platform hierarchy not supported by this command.");
            return false;
        }
        *hierarchy = TPM2_RH_PLATFORM;
        return true;
    }

    bool is_e = !strcmp(value, "e");
    if (is_e) {
        if (!(flags & TPM2_HIERARCHY_FLAGS_E)) {
            LOG_ERR("Endorsement hierarchy not supported by this command.");
            return false;
        }
        *hierarchy = TPM2_RH_ENDORSEMENT;
        return true;
    }

    bool is_n = !strcmp(value, "n");
    if (is_n) {
        if (!(flags & TPM2_HIERARCHY_FLAGS_N)) {
            LOG_ERR("NULL hierarchy not supported by this command.");
            return false;
        }
        *hierarchy = TPM2_RH_NULL;
        return true;
    }

    bool result = tpm2_util_string_to_uint32(value, hierarchy);
    if (!result) {
        LOG_ERR("Incorrect hierarchy value, got: \"%s\", expected [o|p|e|n]"
                "or a number",
            value);
    }

    return result;
}

bool tpm2_hierarchy_create_primary(ESYS_CONTEXT *ectx,
        tpm2_session *sess,
        tpm2_hierarchy_pdata *objdata) {

    ESYS_TR hierarchy;

    hierarchy = tpm2_tpmi_hierarchy_to_esys_tr(objdata->in.hierarchy);

    TSS2_RC rval;
    ESYS_TR shandle1 = tpm2_auth_util_get_shandle(ectx, hierarchy, sess);
    if (shandle1 == ESYS_TR_NONE) {
        LOG_ERR("Couldn't get shandle for hierarchy");
        return false;
    }

    rval = Esys_CreatePrimary(ectx, hierarchy,
            shandle1, ESYS_TR_NONE, ESYS_TR_NONE,
            &objdata->in.sensitive, &objdata->in.public,
            &objdata->in.outside_info, &objdata->in.creation_pcr,
            &objdata->out.handle, &objdata->out.public,
            &objdata->out.creation.data, &objdata->out.hash,
            &objdata->out.creation.ticket);
    if (rval != TPM2_RC_SUCCESS) {
        LOG_PERR(Esys_CreatePrimary, rval);
        return false;
    }

    return true;
}

void tpm2_hierarchy_pdata_free(tpm2_hierarchy_pdata *objdata) {

    free(objdata->out.creation.data);
    objdata->out.creation.data = NULL;
    free(objdata->out.creation.ticket);
    objdata->out.creation.ticket = NULL;
    free(objdata->out.hash);
    objdata->out.hash = NULL;
    free(objdata->out.public);
    objdata->out.public = NULL;
}

/* SPDX-License-Identifier: BSD-3-Clause */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <tss2/tss2_esys.h>

#include "files.h"
#include "log.h"
#include "tpm2_alg_util.h"
#include "tpm2_auth_util.h"
#include "tpm2_options.h"
#include "tpm2_session.h"
#include "tpm2_tool.h"
#include "tpm2_util.h"

typedef struct tpm_pcrevent_ctx tpm_pcrevent_ctx;
struct tpm_pcrevent_ctx {
    struct {
        UINT8 x : 1;
    } flags;
    struct {
        const char *auth_str;
        tpm2_session *session;
    } auth;
    ESYS_TR pcr;
    FILE *input;
};

static tpm_pcrevent_ctx ctx = {
        .pcr = ESYS_TR_RH_NULL,
};

static tool_rc tpm_pcrevent_file(ESYS_CONTEXT *ectx,
        TPML_DIGEST_VALUES **result) {

    TSS2_RC rval;
    unsigned long file_size = 0;
    FILE *input = ctx.input;

    /* Suppress error reporting with NULL path */
    bool res = files_get_file_size(input, &file_size, NULL);

    /* If we can get the file size and its less than 1024, just do it in one hash invocation */
    if (res && file_size <= BUFFER_SIZE(TPM2B_EVENT, buffer)) {

        TPM2B_EVENT buffer = TPM2B_INIT(file_size);

        res = files_read_bytes(ctx.input, buffer.buffer, buffer.size);
        if (!res) {
            LOG_ERR("Error reading input file!");
            return false;
        }

        ESYS_TR shandle1 = tpm2_auth_util_get_shandle(ectx, ctx.pcr,
                                ctx.auth.session);
        if (shandle1 == ESYS_TR_NONE) {
            return false;
        }

        rval = Esys_PCR_Event(ectx, ctx.pcr,
                    shandle1, ESYS_TR_NONE, ESYS_TR_NONE,
                    &buffer, result);
        if (rval != TSS2_RC_SUCCESS) {
            LOG_PERR(Esys_PCR_Event, rval);
            return tool_rc_from_tpm(rval);
        }

        return tool_rc_success;
    }

    ESYS_TR sequence_handle;
    TPM2B_AUTH nullAuth = TPM2B_EMPTY_INIT;

    /*
     * Size is either unknown because the FILE * is a fifo, or it's too big
     * to do in a single hash call. Based on the size figure out the chunks
     * to loop over, if possible. This way we can call Complete with data.
     */
    rval = Esys_HashSequenceStart(ectx,
                ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                &nullAuth, TPM2_ALG_NULL, &sequence_handle);
    if (rval != TPM2_RC_SUCCESS) {
        LOG_PERR(Esys_HashSequenceStart, rval);
        return tool_rc_from_tpm(rval);
    }

    rval = Esys_TR_SetAuth(ectx, sequence_handle, &nullAuth);
    if (rval != TPM2_RC_SUCCESS) {
        LOG_PERR(Esys_TR_SetAuth, rval);
        return tool_rc_from_tpm(rval);
    }

    /* If we know the file size, we decrement the amount read and terminate the
     * loop when 1 block is left, else we go till feof.
     */
    size_t left = file_size;
    bool use_left = !!res;

    TPM2B_MAX_BUFFER data;

    bool done = false;
    while (!done) {

        size_t bytes_read = fread(data.buffer, 1,
                BUFFER_SIZE(typeof(data), buffer), input);
        if (ferror(input)) {
            LOG_ERR("Error reading from input file");
            return false;
        }

        data.size = bytes_read;

        /* if data was read, update the sequence */
        rval = Esys_SequenceUpdate(ectx, sequence_handle,
                    ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                    &data);
        if (rval != TPM2_RC_SUCCESS) {
            LOG_PERR(Esys_SequenceUpdate, rval);
            return tool_rc_from_tpm(rval);
        }

        if (use_left) {
            left -= bytes_read;
            if (left <= TPM2_MAX_DIGEST_BUFFER) {
                done = true;
                continue;
            }
        } else if (feof(input)) {
            done = true;
        }
    } /* end file read/hash update loop */

    if (use_left) {
        data.size = left;
        bool res = files_read_bytes(input, data.buffer, left);
        if (!res) {
            LOG_ERR("Error reading from input file.");
            return tool_rc_general_error;
        }
    } else {
        data.size = 0;
    }

    ESYS_TR shandle1 = tpm2_auth_util_get_shandle(ectx, ctx.pcr,
                            ctx.auth.session);
    if (shandle1 == ESYS_TR_NONE) {
        return tool_rc_general_error;
    }
    rval = Esys_EventSequenceComplete(ectx, ctx.pcr, sequence_handle,
                shandle1, ESYS_TR_PASSWORD, ESYS_TR_NONE,
                &data, result);
    if (rval != TSS2_RC_SUCCESS) {
        LOG_PERR(Esys_EventSequenceComplete, rval);
        return tool_rc_general_error;
    }

    return tool_rc_success;
}

static tool_rc do_pcrevent_and_output(ESYS_CONTEXT *ectx) {

    TPML_DIGEST_VALUES *digests = NULL;
    tool_rc rc = tpm_pcrevent_file(ectx, &digests);
    if (rc != tool_rc_success) {
        return rc;
    }

    assert(digests);

    UINT32 i;
    for (i = 0; i < digests->count; i++) {
        TPMT_HA *d = &digests->digests[i];

        tpm2_tool_output("%s: ", tpm2_alg_util_algtostr(d->hashAlg, tpm2_alg_util_flags_hash));

        BYTE *bytes;
        size_t size;
        switch (d->hashAlg) {
        case TPM2_ALG_SHA1:
            bytes = d->digest.sha1;
            size = sizeof(d->digest.sha1);
            break;
        case TPM2_ALG_SHA256:
            bytes = d->digest.sha256;
            size = sizeof(d->digest.sha256);
            break;
        case TPM2_ALG_SHA384:
            bytes = d->digest.sha384;
            size = sizeof(d->digest.sha384);
            break;
        case TPM2_ALG_SHA512:
            bytes = d->digest.sha512;
            size = sizeof(d->digest.sha512);
            break;
        case TPM2_ALG_SM3_256:
            bytes = d->digest.sm3_256;
            size = sizeof(d->digest.sm3_256);
            break;
        default: {
            LOG_WARN("Unknown digest to convert!");
            // print something so the format doesn't change
            // on this case.
            static BYTE byte = 0;
            bytes = &byte;
            size = sizeof(byte);
        }
        }

        size_t j;
        for (j = 0; j < size; j++) {
            tpm2_tool_output("%02x", bytes[j]);
        }

        tpm2_tool_output("\n");

    }

    free(digests);
    return tool_rc_success;
}

static bool on_arg(int argc, char **argv) {

    if (argc > 1) {
        LOG_ERR("Expected a single FILE argument, got: %d", argc);
        return false;
    }

    ctx.input = fopen(argv[0], "rb");
    if (!ctx.input) {
        LOG_ERR("Error opening file \"%s\", error: %s", argv[0],
                strerror(errno));
        return false;
    }

    return true;
}

static bool on_option(char key, char *value) {

    switch (key) {
    case 'x': {
        bool result = tpm2_util_string_to_uint32(value, &ctx.pcr);
        if (!result) {
            LOG_ERR("Could not convert \"%s\", to a pcr index.", value);
            return false;
        }
    }
        ctx.flags.x = 1;
        break;
    case 'P':
        ctx.auth.auth_str = value;
        break;
        /* no default */
    }

    return true;
}

bool tpm2_tool_onstart(tpm2_options **opts) {

    static const struct option topts[] = {
        { "pcr-index", required_argument, NULL, 'x' },
        { "auth-pcr",  required_argument, NULL, 'P' },
    };

    *opts = tpm2_options_new("x:P:", ARRAY_LEN(topts), topts,
                             on_option, on_arg, 0);

    return *opts != NULL;
}

tool_rc tpm2_tool_onrun(ESYS_CONTEXT *ectx, tpm2_option_flags flags) {

    UNUSED(flags);

    ctx.input = ctx.input ? ctx.input : stdin;

    bool result = tpm2_auth_util_from_optarg(ectx, ctx.auth.auth_str,
            &ctx.auth.session, false);
    if (!result) {
        LOG_ERR("Invalid key handle authorization, got\"%s\"",
            ctx.auth.auth_str);
        return tool_rc_general_error;
    }

    return do_pcrevent_and_output(ectx);
}

tool_rc tpm2_tool_onstop(ESYS_CONTEXT *ectx) {
    UNUSED(ectx);
    return tpm2_session_close(&ctx.auth.session);
}

void tpm2_tool_onexit(void) {

    if (ctx.input && ctx.input != stdin) {
        fclose(ctx.input);
    }
}

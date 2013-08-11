/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


static char *RCSSTRING __UNUSED__="$Id: stun_proc.c,v 1.2 2008/04/28 18:21:30 ekr Exp $";

#include <errno.h>
#include <csi_platform.h>

#ifdef WIN32
#include <winsock2.h>
#include <stdlib.h>
#include <io.h>
#include <time.h>
#else   /* UNIX */
#include <string.h>
#endif  /* end UNIX */
#include <assert.h>

#include "stun.h"
#include "stun_reg.h"
#include "registry.h"

static int
nr_stun_add_realm_and_nonce(int new_nonce, nr_stun_server_client *clnt, nr_stun_message *res);

/* draft-ietf-behave-rfc3489bis-10.txt S 7.3 */
int
nr_stun_receive_message(nr_stun_message *req, nr_stun_message *msg)
{
    int _status;
    nr_stun_message_attribute *attr;

#ifdef USE_RFC_3489_BACKWARDS_COMPATIBLE
    /* if this message was generated by an RFC 3489 impementation,
     * the call to nr_is_stun_message will fail, so skip that
     * check and puke elsewhere if the message can't be decoded */
    if (msg->header.magic_cookie == NR_STUN_MAGIC_COOKIE
     || msg->header.magic_cookie == NR_STUN_MAGIC_COOKIE2) {
#endif /* USE_RFC_3489_BACKWARDS_COMPATIBLE */
    if (!nr_is_stun_message(msg->buffer, msg->length)) {
        r_log(NR_LOG_STUN, LOG_DEBUG, "Not a STUN message");
        ABORT(R_REJECTED);
    }
#ifdef USE_RFC_3489_BACKWARDS_COMPATIBLE
    }
#endif /* USE_RFC_3489_BACKWARDS_COMPATIBLE */

    if (req == 0) {
        if (NR_STUN_GET_TYPE_CLASS(msg->header.type) != NR_CLASS_REQUEST) {
            r_log(NR_LOG_STUN,LOG_NOTICE,"Illegal message type: %03x", msg->header.type);
            ABORT(R_REJECTED);
        }
    }
    else {
        if (NR_STUN_GET_TYPE_CLASS(msg->header.type) != NR_CLASS_RESPONSE
         && NR_STUN_GET_TYPE_CLASS(msg->header.type) != NR_CLASS_ERROR_RESPONSE) {
            r_log(NR_LOG_STUN,LOG_NOTICE,"Illegal message class: %03x", msg->header.type);
            ABORT(R_REJECTED);
        }

        if (NR_STUN_GET_TYPE_METHOD(req->header.type) != NR_STUN_GET_TYPE_METHOD(msg->header.type)) {
            r_log(NR_LOG_STUN,LOG_NOTICE,"Inconsistent message method: %03x", msg->header.type);
            ABORT(R_REJECTED);
        }

        if (nr_stun_different_transaction(msg->buffer, msg->length, req)) {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Unrecognized STUN transaction");
            ABORT(R_REJECTED);
        }
    }

    switch (msg->header.magic_cookie) {
    case NR_STUN_MAGIC_COOKIE:
        /* basically draft-ietf-behave-rfc3489bis-10.txt S 6 rules */

        if (nr_stun_message_has_attribute(msg, NR_STUN_ATTR_FINGERPRINT, &attr)
         && !attr->u.fingerprint.valid) {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Invalid fingerprint");
            ABORT(R_REJECTED);
        }

        break;

#ifdef USE_STUND_0_96
    case NR_STUN_MAGIC_COOKIE2:
        /* nothing to check in this case */
        break;
#endif /* USE_STUND_0_96 */

    default:
#ifdef USE_RFC_3489_BACKWARDS_COMPATIBLE
        /* in RFC 3489 there is no magic cookie, it's part of the transaction ID */
#else
#ifdef NDEBUG
        /* in deployment builds we should always see a recognized magic cookie */
        r_log(NR_LOG_STUN, LOG_ERR, "Missing Magic Cookie");
        ABORT(R_REJECTED);
#else
        /* ignore this condition because sometimes we like to pretend we're
         * a server talking to old clients and their messages don't contain
         * a magic cookie at all but rather the magic cookie field is part
         * of their ID and therefore random */
#endif /* NDEBUG */
#endif /* USE_RFC_3489_BACKWARDS_COMPATIBLE */
        break;
    }

    _status=0;
  abort:
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 7.3.1 */
int
nr_stun_process_request(nr_stun_message *req, nr_stun_message *res)
{
    int _status;
#ifdef USE_STUN_PEDANTIC
    int r;
    nr_stun_attr_unknown_attributes unknown_attributes = { { 0 } };
    nr_stun_message_attribute *attr;

    if (req->comprehension_required_unknown_attributes > 0) {
        nr_stun_form_error_response(req, res, 420, "Unknown Attributes");

        TAILQ_FOREACH(attr, &req->attributes, entry) {
            if (attr->name == 0) {
                /* unrecognized attribute */

                /* should never happen, but truncate if it ever were to occur */
                if (unknown_attributes.num_attributes > NR_STUN_MAX_UNKNOWN_ATTRIBUTES)
                    break;

                unknown_attributes.attribute[unknown_attributes.num_attributes++] = attr->type;
            }
        }

        assert(req->comprehension_required_unknown_attributes + req->comprehension_optional_unknown_attributes == unknown_attributes.num_attributes);

        if ((r=nr_stun_message_add_unknown_attributes_attribute(res, &unknown_attributes)))
            ABORT(R_ALREADY);

        ABORT(R_ALREADY);
    }
#endif /* USE_STUN_PEDANTIC */

    _status=0;
#ifdef USE_STUN_PEDANTIC
  abort:
#endif /* USE_STUN_PEDANTIC */
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 7.3.2 */
int
nr_stun_process_indication(nr_stun_message *ind)
{
    int _status;
#ifdef USE_STUN_PEDANTIC

    if (ind->comprehension_required_unknown_attributes > 0)
        ABORT(R_REJECTED);
#endif /* USE_STUN_PEDANTIC */

    _status=0;
#ifdef USE_STUN_PEDANTIC
  abort:
#endif /* USE_STUN_PEDANTIC */
    return _status;
}

/* RFC5389 S 7.3.3, except that we *also* allow a MAPPED_ADDRESS
   to compensate for a bug in Google's STUN server where it
   always returns MAPPED_ADDRESS.

   Mozilla bug: 888274.
 */
int
nr_stun_process_success_response(nr_stun_message *res)
{
    int _status;

#ifdef USE_STUN_PEDANTIC
    if (res->comprehension_required_unknown_attributes > 0)
        ABORT(R_REJECTED);
#endif /* USE_STUN_PEDANTIC */

    if (NR_STUN_GET_TYPE_METHOD(res->header.type) == NR_METHOD_BINDING) {
        if (! nr_stun_message_has_attribute(res, NR_STUN_ATTR_XOR_MAPPED_ADDRESS, 0) &&
            ! nr_stun_message_has_attribute(res, NR_STUN_ATTR_MAPPED_ADDRESS, 0)) {
            r_log(NR_LOG_STUN, LOG_ERR, "Missing XOR-MAPPED-ADDRESS and MAPPED_ADDRESS");
            ABORT(R_REJECTED);
        }
    }

    _status=0;
 abort:
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 7.3.4 */
int
nr_stun_process_error_response(nr_stun_message *res, UINT2 *error_code)
{
    int _status;
    nr_stun_message_attribute *attr;

    if (res->comprehension_required_unknown_attributes > 0) {
        ABORT(R_REJECTED);
    }

    if (! nr_stun_message_has_attribute(res, NR_STUN_ATTR_ERROR_CODE, &attr)) {
        r_log(NR_LOG_STUN, LOG_ERR, "Missing ERROR-CODE");
        ABORT(R_REJECTED);
    }

    *error_code = attr->u.error_code.number;

    switch (attr->u.error_code.number / 100) {
    case 3:
        /* If the error code is 300 through 399, the client SHOULD consider
         * the transaction as failed unless the ALTERNATE-SERVER extension is
         * being used.  See Section 11. */

        if (attr->u.error_code.number == 300) {
            if (!nr_stun_message_has_attribute(res, NR_STUN_ATTR_ALTERNATE_SERVER, 0)) {
                r_log(NR_LOG_STUN, LOG_ERR, "Missing ALTERNATE-SERVER");
                ABORT(R_REJECTED);
            }

            /* draft-ietf-behave-rfc3489bis-10.txt S 11 */
            if (!nr_stun_message_has_attribute(res, NR_STUN_ATTR_MESSAGE_INTEGRITY, 0)) {
                r_log(NR_LOG_STUN, LOG_ERR, "Missing MESSAGE-INTEGRITY");
                ABORT(R_REJECTED);
            }

            ABORT(R_RETRY);
        }

        ABORT(R_REJECTED);
        break;

    case 4:
        /* If the error code is 400 through 499, the client declares the
         * transaction failed; in the case of 420 (Unknown Attribute), the
         * response should contain a UNKNOWN-ATTRIBUTES attribute that gives
         * additional information. */
        if (attr->u.error_code.number == 420)
            ABORT(R_REJECTED);

        /* it may be possible to restart given the info that was received in
         * this response, so retry */
        ABORT(R_RETRY);
        break;

    case 5:
        /* If the error code is 500 through 599, the client MAY resend the
         * request; clients that do so MUST limit the number of times they do
         * this. */
        /* let the retransmit mechanism handle resending the request */
        break;

    default:
        ABORT(R_REJECTED);
        break;
    }

    /* the spec says: "The client then does any processing specified by the authentication
     * mechanism (see Section 10).  This may result in a new transaction
     * attempt." -- but this is handled already elsewhere, so needn't be repeated
     * in this function */

    _status=0;
 abort:
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 10.1.2 */
int
nr_stun_receive_request_or_indication_short_term_auth(nr_stun_message *msg,
                                                      nr_stun_message *res)
{
    int _status;
    nr_stun_message_attribute *attr;

    switch (msg->header.magic_cookie) {
    default:
        /* in RFC 3489 there is no magic cookie, it's part of the transaction ID */
        /* drop thru */
    case NR_STUN_MAGIC_COOKIE:
        if (!nr_stun_message_has_attribute(msg, NR_STUN_ATTR_MESSAGE_INTEGRITY, &attr)) {
            nr_stun_form_error_response(msg, res, 400, "Missing MESSAGE-INTEGRITY");
            ABORT(R_ALREADY);
        }

        if (!nr_stun_message_has_attribute(msg, NR_STUN_ATTR_USERNAME, 0)) {
            nr_stun_form_error_response(msg, res, 400, "Missing USERNAME");
            ABORT(R_ALREADY);
        }

        if (attr->u.message_integrity.unknown_user) {
            nr_stun_form_error_response(msg, res, 401, "Unrecognized USERNAME");
            ABORT(R_ALREADY);
        }

        if (!attr->u.message_integrity.valid) {
            nr_stun_form_error_response(msg, res, 401, "Bad MESSAGE-INTEGRITY");
            ABORT(R_ALREADY);
        }

        break;

#ifdef USE_STUND_0_96
    case NR_STUN_MAGIC_COOKIE2:
        /* nothing to check in this case */
        break;
#endif /* USE_STUND_0_96 */
    }

    _status=0;
 abort:
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 10.1.3 */
int
nr_stun_receive_response_short_term_auth(nr_stun_message *res)
{
    int _status;
    nr_stun_message_attribute *attr;

    switch (res->header.magic_cookie) {
    default:
        /* in RFC 3489 there is no magic cookie, it's part of the transaction ID */
        /* drop thru */
    case NR_STUN_MAGIC_COOKIE:
        if (!nr_stun_message_has_attribute(res, NR_STUN_ATTR_MESSAGE_INTEGRITY, &attr)) {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Missing MESSAGE-INTEGRITY");
            ABORT(R_REJECTED);
        }

        if (!attr->u.message_integrity.valid) {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Bad MESSAGE-INTEGRITY");
            ABORT(R_REJECTED);
        }

        break;

#ifdef USE_STUND_0_96
    case NR_STUN_MAGIC_COOKIE2:
        /* nothing to check in this case */
        break;
#endif /* USE_STUND_0_96 */
    }

   _status=0;
 abort:
     return _status;
}

static int
nr_stun_add_realm_and_nonce(int new_nonce, nr_stun_server_client *clnt, nr_stun_message *res)
{
    int r,_status;
    char *realm = 0;
    char *nonce;
    UINT2 size;

    if ((r=NR_reg_alloc_string(NR_STUN_REG_PREF_SERVER_REALM, &realm)))
        ABORT(r);

    if ((r=nr_stun_message_add_realm_attribute(res, realm)))
        ABORT(r);

    if (clnt) {
        if (strlen(clnt->nonce) < 1)
            new_nonce = 1;

        if (new_nonce) {
            if (NR_reg_get_uint2(NR_STUN_REG_PREF_SERVER_NONCE_SIZE, &size))
                size = 48;

            if (size > (sizeof(clnt->nonce) - 1))
                size = sizeof(clnt->nonce) - 1;

            nr_random_alphanum(clnt->nonce, size);
            clnt->nonce[size] = '\0';
        }

        nonce = clnt->nonce;
    }
    else {
        /* user is not known, so use a bogus nonce since there's no way to
         * store a good nonce with the client-specific data -- this nonce
         * will be recognized as stale if the client attempts another
         * request */
        nonce = "STALE";
    }

    if ((r=nr_stun_message_add_nonce_attribute(res, nonce)))
        ABORT(r);

    _status=0;
 abort:
#ifdef USE_TURN
assert(_status == 0); /* TODO: !nn! cleanup after I reimplmement TURN */
#endif
    RFREE(realm);
    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 10.2.1 - 10.2.2 */
int
nr_stun_receive_request_long_term_auth(nr_stun_message *req, nr_stun_server_ctx *ctx, nr_stun_message *res)
{
    int r,_status;
    nr_stun_message_attribute *mi;
    nr_stun_message_attribute *n;
    nr_stun_server_client *clnt = 0;

    switch (req->header.magic_cookie) {
    default:
        /* in RFC 3489 there is no magic cookie, it's part of the transaction ID */
        /* drop thru */
    case NR_STUN_MAGIC_COOKIE:
        if (!nr_stun_message_has_attribute(req, NR_STUN_ATTR_USERNAME, 0)) {
            nr_stun_form_error_response(req, res, 400, "Missing USERNAME");
            nr_stun_add_realm_and_nonce(0, 0, res);
            ABORT(R_ALREADY);
        }

        if ((r=nr_stun_get_message_client(ctx, req, &clnt))) {
            nr_stun_form_error_response(req, res, 401, "Unrecognized USERNAME");
            nr_stun_add_realm_and_nonce(0, 0, res);
            ABORT(R_ALREADY);
        }

        if (!nr_stun_message_has_attribute(req, NR_STUN_ATTR_MESSAGE_INTEGRITY, &mi)) {
            nr_stun_form_error_response(req, res, 401, "Missing MESSAGE-INTEGRITY");
            nr_stun_add_realm_and_nonce(0, clnt, res);
            ABORT(R_ALREADY);
        }

        assert(!mi->u.message_integrity.unknown_user);

        if (!nr_stun_message_has_attribute(req, NR_STUN_ATTR_REALM, 0)) {
            nr_stun_form_error_response(req, res, 400, "Missing REALM");
            ABORT(R_ALREADY);
        }

        if (!nr_stun_message_has_attribute(req, NR_STUN_ATTR_NONCE, &n)) {
            nr_stun_form_error_response(req, res, 400, "Missing NONCE");
            ABORT(R_ALREADY);
        }

        assert(sizeof(clnt->nonce) == sizeof(n->u.nonce));
        if (strncmp(clnt->nonce, n->u.nonce, sizeof(n->u.nonce))) {
            nr_stun_form_error_response(req, res, 438, "Stale NONCE");
            nr_stun_add_realm_and_nonce(1, clnt, res);
            ABORT(R_ALREADY);
        }

        if (!mi->u.message_integrity.valid) {
            nr_stun_form_error_response(req, res, 401, "Bad MESSAGE-INTEGRITY");
            nr_stun_add_realm_and_nonce(0, clnt, res);
            ABORT(R_ALREADY);
        }

        break;

#ifdef USE_STUND_0_96
    case NR_STUN_MAGIC_COOKIE2:
        /* nothing to do in this case */
        break;
#endif /* USE_STUND_0_96 */
    }

    _status=0;
 abort:

    return _status;
}

/* draft-ietf-behave-rfc3489bis-10.txt S 10.2.3 */
int
nr_stun_receive_response_long_term_auth(nr_stun_message *res, nr_stun_client_ctx *ctx)
{
    int _status;
    nr_stun_message_attribute *attr;

    switch (res->header.magic_cookie) {
    default:
        /* in RFC 3489 there is no magic cookie, it's part of the transaction ID */
        /* drop thru */
    case NR_STUN_MAGIC_COOKIE:
        if (nr_stun_message_has_attribute(res, NR_STUN_ATTR_REALM, &attr)) {
            RFREE(ctx->realm);
            ctx->realm = r_strdup(attr->u.realm);
            if (!ctx->realm)
                ABORT(R_NO_MEMORY);
        }
        else {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Missing REALM");
            ABORT(R_REJECTED);
        }

        if (nr_stun_message_has_attribute(res, NR_STUN_ATTR_NONCE, &attr)) {
            RFREE(ctx->nonce);
            ctx->nonce = r_strdup(attr->u.nonce);
            if (!ctx->nonce)
                ABORT(R_NO_MEMORY);
        }
        else {
            r_log(NR_LOG_STUN, LOG_DEBUG, "Missing NONCE");
            ABORT(R_REJECTED);
        }

        if (nr_stun_message_has_attribute(res, NR_STUN_ATTR_MESSAGE_INTEGRITY, &attr)) {
            if (!attr->u.message_integrity.valid) {
                r_log(NR_LOG_STUN, LOG_DEBUG, "Bad MESSAGE-INTEGRITY");
                ABORT(R_REJECTED);
            }
        }

        break;

#ifdef USE_STUND_0_96
    case NR_STUN_MAGIC_COOKIE2:
        /* nothing to check in this case */
        break;
#endif /* USE_STUND_0_96 */
    }

    _status=0;
 abort:
    return _status;
}


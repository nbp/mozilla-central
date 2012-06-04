/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMError_h__
#define nsDOMError_h__

#include "nsError.h"

// XXX If you add a new error code, also add an error string to
// dom/src/base/domerr.msg

/* Standard DOM error codes: http://dvcs.w3.org/hg/domcore/raw-file/tip/Overview.html */

#define NS_ERROR_DOM_INDEX_SIZE_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1)
#define NS_ERROR_DOM_HIERARCHY_REQUEST_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,3)
#define NS_ERROR_DOM_WRONG_DOCUMENT_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,4)
#define NS_ERROR_DOM_INVALID_CHARACTER_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,5)
#define NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,7)
#define NS_ERROR_DOM_NOT_FOUND_ERR               NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,8)
#define NS_ERROR_DOM_NOT_SUPPORTED_ERR           NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,9)
#define NS_ERROR_DOM_INUSE_ATTRIBUTE_ERR         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,10)
#define NS_ERROR_DOM_INVALID_STATE_ERR           NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,11)
#define NS_ERROR_DOM_SYNTAX_ERR                  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,12)
#define NS_ERROR_DOM_INVALID_MODIFICATION_ERR    NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,13)
#define NS_ERROR_DOM_NAMESPACE_ERR               NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,14)
#define NS_ERROR_DOM_INVALID_ACCESS_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,15)
#define NS_ERROR_DOM_TYPE_MISMATCH_ERR           NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,17)
#define NS_ERROR_DOM_SECURITY_ERR                NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,18)
#define NS_ERROR_DOM_NETWORK_ERR                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,19)
#define NS_ERROR_DOM_ABORT_ERR                   NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,20)
#define NS_ERROR_DOM_URL_MISMATCH_ERR            NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,21)
#define NS_ERROR_DOM_QUOTA_EXCEEDED_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,22)
#define NS_ERROR_DOM_TIMEOUT_ERR                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,23)
#define NS_ERROR_DOM_INVALID_NODE_TYPE_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,24)
#define NS_ERROR_DOM_DATA_CLONE_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,25)

/* XXX Should be JavaScript native TypeError */

#define NS_ERROR_TYPE_ERR                        NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,26)

/* SVG DOM error codes from http://www.w3.org/TR/SVG11/svgdom.html */

#define NS_ERROR_DOM_SVG_WRONG_TYPE_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_SVG,0)
#define NS_ERROR_DOM_SVG_INVALID_VALUE_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_SVG,1)
#define NS_ERROR_DOM_SVG_MATRIX_NOT_INVERTABLE   NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_SVG,2)

/* DOM error codes from http://www.w3.org/TR/DOM-Level-3-XPath/ */

#define NS_ERROR_DOM_INVALID_EXPRESSION_ERR      NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_XPATH, 51)
#define NS_ERROR_DOM_TYPE_ERR                    NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_XPATH, 52)

/* IndexedDB error codes http://dvcs.w3.org/hg/IndexedDB/raw-file/tip/Overview.html */

#define NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,1)
#define NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR     NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,3)
#define NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR    NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,4)
#define NS_ERROR_DOM_INDEXEDDB_DATA_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,5)
#define NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR   NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,6)
#define NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR \
                                                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,7)
#define NS_ERROR_DOM_INDEXEDDB_ABORT_ERR         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,8)
#define NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR     NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,9)
#define NS_ERROR_DOM_INDEXEDDB_TIMEOUT_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,10)
#define NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,11)
#define NS_ERROR_DOM_INDEXEDDB_VERSION_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,12)

#define NS_ERROR_DOM_INDEXEDDB_RECOVERABLE_ERR   NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_INDEXEDDB,1001)

/* DOM error codes defined by us */

#define NS_ERROR_DOM_SECMAN_ERR                  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1001)
#define NS_ERROR_DOM_WRONG_TYPE_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1002)
#define NS_ERROR_DOM_NOT_OBJECT_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1003)
#define NS_ERROR_DOM_NOT_XPC_OBJECT_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1004)
#define NS_ERROR_DOM_NOT_NUMBER_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1005)
#define NS_ERROR_DOM_NOT_BOOLEAN_ERR             NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1006)
#define NS_ERROR_DOM_NOT_FUNCTION_ERR            NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1007)
#define NS_ERROR_DOM_TOO_FEW_PARAMETERS_ERR      NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1008)
#define NS_ERROR_DOM_BAD_DOCUMENT_DOMAIN         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1009)
#define NS_ERROR_DOM_PROP_ACCESS_DENIED          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1010)
#define NS_ERROR_DOM_XPCONNECT_ACCESS_DENIED     NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1011)
#define NS_ERROR_DOM_BAD_URI                     NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1012)
#define NS_ERROR_DOM_RETVAL_UNDEFINED            NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1013)
#define NS_ERROR_DOM_QUOTA_REACHED               NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM,1014)

#define NS_ERROR_DOM_FILE_NOT_FOUND_ERR          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILE, 0)
#define NS_ERROR_DOM_FILE_NOT_READABLE_ERR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILE, 1)
#define NS_ERROR_DOM_FILE_ABORT_ERR              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILE, 2)

#define NS_ERROR_DOM_FILEHANDLE_UNKNOWN_ERR      NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILEHANDLE,1)
#define NS_ERROR_DOM_FILEHANDLE_NOT_ALLOWED_ERR  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILEHANDLE,2)
#define NS_ERROR_DOM_FILEHANDLE_LOCKEDFILE_INACTIVE_ERR \
                                                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILEHANDLE,3)
#define NS_ERROR_DOM_FILEHANDLE_ABORT_ERR        NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILEHANDLE,4)
#define NS_ERROR_DOM_FILEHANDLE_READ_ONLY_ERR    NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_DOM_FILEHANDLE,5)

#endif // nsDOMError_h__

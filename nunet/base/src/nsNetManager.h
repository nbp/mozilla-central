/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#ifndef _nsNetManager_h_
#define _nsNetManager_h_

//#include "nsISupports.h"
/* 
    The nsNetManager class is the central service that
    runs the core nunet architecture. It handles all
    miscellaneous items like initializations, etc. 

    //TODO add the list of things this class does.
    -Gagan Saksena 03/23/99
*/

class nsNetManager //: public nsISupports
{

public:
    static nsNetManager*   GetInstance(void)
    {
        static nsNetManager* pNetMgr = new nsNetManager();
        //pNetMgr->AddRef();
        return pNetMgr;
    }

    void                    Release(void);

protected:
    nsNetManager(void);
    virtual ~nsNetManager();

};

#endif /* _nsNetManager_h_ */

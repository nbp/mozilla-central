/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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

/*
* ListView that supports letting ListItems handle their own events
*/

// when     who     what
// 10/31/97 jband   added this file
//

package com.netscape.jsdebugging.ifcui;

import netscape.application.*;
import netscape.util.*;

public class SmartItemListView extends BackgroundHackListView
{
    public SmartItemListView()
    {
        super();
    }

    // pass these events along to the item

    public boolean mouseDown(MouseEvent me)
    {
        SmartListItem sli = smartItemFromEvent(me);
        if(null!=sli)
            return sli.mouseDown(me);
        return false;
    }
    public void mouseDragged(MouseEvent me)
    {
        SmartListItem sli = smartItemFromEvent(me);
        if(null!=sli)
            sli.mouseDragged(me);
    }
    public void mouseUp(MouseEvent me)
    {
        SmartListItem sli = smartItemFromEvent(me);
        if(null!=sli)
            sli.mouseUp(me);
    }

    // these provide an easy way to ask for default behavior
    public boolean superMouseDown(MouseEvent me)
    {
        return super.mouseDown(me);
    }
    public void superMouseDragged(MouseEvent me)
    {
        super.mouseDragged(me);
    }
    public void superMouseUp(MouseEvent me)
    {
        super.mouseUp(me);
    }

    private final SmartListItem smartItemFromEvent(MouseEvent me)
    {
        return (SmartListItem) itemForPoint(me.x, me.y);
    }
}



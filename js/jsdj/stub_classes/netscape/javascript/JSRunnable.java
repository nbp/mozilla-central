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

/* jband Sept 1998 - **NOT original file** - copied here for simplicity */

package netscape.javascript;

/**
 * Runs a JavaScript object with a run() method in a separate thread.
 */
public class JSRunnable implements Runnable {
	private JSObject runnable;

	public JSRunnable(JSObject runnable) {
		this.runnable = runnable;
		synchronized(this) {
			new Thread(this).start();
			try {
				this.wait();
			} catch (InterruptedException ie) {
			}
		}
	}
	
	public void run() {
		try {
			runnable.call("run", null);
			synchronized(this) {
				notifyAll();
			}
		} catch (Throwable t) {
			System.err.println(t);
			t.printStackTrace(System.err);
		}
	}
}

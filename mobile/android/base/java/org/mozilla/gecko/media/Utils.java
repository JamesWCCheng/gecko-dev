/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.util.Log;

public class Utils {
    public static long getThreadId() {
        Thread t = Thread.currentThread();
        return t.getId();
    }

    public static String getThreadSignature() {
        Thread t = Thread.currentThread();
        long l = t.getId();
        String name = t.getName();
        long p = t.getPriority();
        String gname = t.getThreadGroup().getName();
        return (name
                + ":(id)" + l
                + ":(priority)" + p
                + ":(group)" + gname);
    }

    public static void logThreadSignature() {
        Log.d("ThreadUtils", getThreadSignature());
    }

    public static void sleepForInSecs(int secs) {
        try {
            Thread.sleep(secs * 1000);
        } catch (InterruptedException x) {
            throw new RuntimeException("interrupted", x);
        }
    }

    public static String getCallStack() {
        String bt = "=========== Callstack ===========\n";
        for (StackTraceElement ste : Thread.currentThread().getStackTrace()) {
            bt += (ste + "\n");
        }
        bt += "======================\n";
        return bt;
    }
}
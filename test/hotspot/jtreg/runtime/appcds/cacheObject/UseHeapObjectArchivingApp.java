/*
 * Copyright (c) 2020, Google Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

import sun.hotspot.WhiteBox;

// Test objects should be archived in heap when UseHeapObjectArchiving is True at both dump time
// and runtime.
public class UseHeapObjectArchivingApp {

    // Static String field with initial value
    static final String archived_field = "abc";

    public UseHeapObjectArchivingApp() {}

    public static void main(String args[]) throws Exception {
        WhiteBox wb = WhiteBox.getWhiteBox();

        new UseHeapObjectArchivingApp().test(wb);
    }

    public void test(WhiteBox wb) {
        Class c = UseHeapObjectArchivingApp.class;
        if (wb.isSharedClass(c)) {
            boolean is_class_archived = wb.isShared(c);
            boolean is_field_archived = wb.isShared(archived_field);

            if (is_class_archived && is_field_archived) {
              System.out.println(c + " class and its field are archived.");
            } else if (!is_class_archived && !is_field_archived) {
              System.out.println(c + " class and its field are not archived.");
            } else {
              throw new RuntimeException("ERROR: discrepancy between archival state of class " +
                                         "object " + c + " and its field.");
            }

            // GC should not crash
            System.gc();
            System.gc();
            System.gc();
        }
    }
}

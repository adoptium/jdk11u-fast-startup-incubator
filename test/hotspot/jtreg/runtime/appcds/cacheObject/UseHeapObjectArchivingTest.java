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

/*
 * @test
 * @summary Test UseHeapObjectArchiving flag.
 * @requires vm.cds.archived.java.heap
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules java.base/jdk.internal.misc
 *          java.management
 *          jdk.jartool/sun.tools.jar
 * @build sun.hotspot.WhiteBox
 * @compile UseHeapObjectArchivingApp.java
 * @run driver ClassFileInstaller -jar app.jar UseHeapObjectArchivingApp
 * @run driver ClassFileInstaller -jar WhiteBox.jar sun.hotspot.WhiteBox
 * @run main UseHeapObjectArchivingTest
 */

import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;


public class UseHeapObjectArchivingTest {
    public static void main(String[] args) throws Exception {
        String wbJar = ClassFileInstaller.getJarPath("WhiteBox.jar");
        String use_whitebox_jar = "-Xbootclasspath/a:" + wbJar;
        String appJar = ClassFileInstaller.getJarPath("app.jar");

        String classlist[] = new String[] {
            "UseHeapObjectArchivingApp",
        };

        // Disable heap object archiving at dump time, and generate a dump.
        TestCommon.testDump(appJar, classlist, use_whitebox_jar, "-XX:-UseHeapObjectArchiving");

        // Test case where we disable heap object archiving at dump time and runtime.
        OutputAnalyzer output = TestCommon.exec(appJar, use_whitebox_jar,
                                                "-XX:-UseHeapObjectArchiving",
                                                "-XX:+WhiteBoxAPI",
                                                "-XX:+VerifyAfterGC",
                                                "UseHeapObjectArchivingApp");
        TestCommon.checkExec(output,
                             "class " + classlist[0] + " class and its field are not archived");

        // Test case where we disable heap object archiving at dump time but enable at runtime.
        output = TestCommon.exec(appJar, use_whitebox_jar,
                                 "-XX:+UseHeapObjectArchiving",
                                 "-XX:+WhiteBoxAPI",
                                 "-XX:+VerifyAfterGC",
                                 "UseHeapObjectArchivingApp");
        TestCommon.checkExec(output,
                             "class " + classlist[0] + " class and its field are not archived");

        // Enable heap object archiving at dump time, and generate a dump.
        TestCommon.testDump(appJar, classlist, use_whitebox_jar, "-XX:+UseHeapObjectArchiving");

        // Test case where we enable heap object archiving at dump time and runtime.
        output = TestCommon.exec(appJar, use_whitebox_jar,
                                 "-XX:+UseHeapObjectArchiving",
                                 "-XX:+WhiteBoxAPI",
                                 "-XX:+VerifyAfterGC",
                                 "UseHeapObjectArchivingApp");
        TestCommon.checkExec(output,
                             "class " + classlist[0] + " class and its field are archived");

        // Test case where we enable heap object archiving at dump time but disable at runtime.
        output = TestCommon.exec(appJar, use_whitebox_jar,
                                 "-XX:-UseHeapObjectArchiving",
                                 "-XX:+WhiteBoxAPI",
                                 "-XX:+VerifyAfterGC",
                                 "UseHeapObjectArchivingApp");
        TestCommon.checkExec(output,
                             "class " + classlist[0] + " class and its field are not archived");
    }
}

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
 *
 */

/*
 * @test
 * @summary Test -XX:[+|-]PreInitializeArchivedClass JVM option.
 * @requires vm.cds.archived.java.heap
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules java.base/jdk.internal.misc
 *          java.management
 *          jdk.jartool/sun.tools.jar
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm PreInitializeArchivedClassFlag
 */

import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;

public class PreInitializeArchivedClassFlag {

    public static void main(String[] args) throws Exception {
        JarBuilder.build(
            "preInitFlag",
            "PreInitializeArchivedClassFlag",
            "PreInitializeArchivedClassFlag$PreInitializeArchivedClassApp");
        String appJar = TestCommon.getTestJar("preInitFlag.jar");

        JarBuilder.build(true, "WhiteBox", "sun/hotspot/WhiteBox");
        String whiteBoxJar = TestCommon.getTestJar("WhiteBox.jar");
        String use_whitebox_jar = "-Xbootclasspath/a:" + whiteBoxJar;

        // Test case 1)
        // - Dump default archive with default option
        //   (-XX:+PreInitializeArchivedClass is the default)
        OutputAnalyzer output = TestCommon.dump(
            appJar,
            TestCommon.list(
                "PreInitializeArchivedClassFlag$PreInitializeArchivedClassApp"),
            use_whitebox_jar);
        TestCommon.checkDump(output);

        System.out.println("------------------ Test case 1 -----------------");
        output = TestCommon.exec(appJar, use_whitebox_jar,
                "-XX:+UnlockDiagnosticVMOptions",
                "-XX:+WhiteBoxAPI",
                "PreInitializeArchivedClassFlag$PreInitializeArchivedClassApp",
                "true");
        TestCommon.checkExec(output);

        // Test case 2)
        //   Dump with -XX:-PreInitializeArchivedClass to disable class
        //   pre-initialization.
        System.out.println("------------------ Test case 2 -----------------");

        output = TestCommon.dump(
            appJar,
            TestCommon.list(
                "PreInitializeArchivedClassFlag$PreInitializeArchivedClassApp"),
            "-XX:-PreInitializeArchivedClass",
            use_whitebox_jar);
        TestCommon.checkDump(output);

        output = TestCommon.exec(appJar, use_whitebox_jar,
                "-XX:+UnlockDiagnosticVMOptions",
                "-XX:+WhiteBoxAPI",
                "PreInitializeArchivedClassFlag$PreInitializeArchivedClassApp",
                "false");
        TestCommon.checkExec(output);
    }

    static class PreInitializeArchivedClassApp {
        public static void main(String[] args) {
            WhiteBox wb = WhiteBox.getWhiteBox();

            boolean preInitialized = Boolean.parseBoolean(args[0]);

            // The boxed Integer for [-128, 127] are cached by default, per JLS.
            // When class pre-initialization is enabled, the cached Integer
            // objects are archived Java objects (no need to dynamically
            // create them at runtime).
            for (int i = -128; i <= 127; i++) {
                if (preInitialized) {
                    if (!wb.isShared(Integer.valueOf(i))) {
                        throw new RuntimeException(
                            "Failed. Integer cached objects are not pre-initialized");
                    }
                } else {
                    if (wb.isShared(Integer.valueOf(i))) {
                        throw new RuntimeException(
                            "Failed. Unexpected pre-initialized Integer cached objects");
                    }
                }
            }
        }
    }
}

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
 * @test PreserveEnumTest
 * @summary Enum annotated using Preserve is fully pre-initialized.
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm PreserveEnumTest
 */

import java.nio.file.StandardOpenOption;
import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;

public class PreserveEnumTest {
    public static void main(String[] args) throws Exception {
        JarBuilder.build("preserveEnum",
                         "PreserveEnumTest",
                         "PreserveEnumTest$PreserveEnumApp");

        String appJar = TestCommon.getTestJar("preserveEnum.jar");
        JarBuilder.build(true, "WhiteBox", "sun/hotspot/WhiteBox");
        String whiteBoxJar = TestCommon.getTestJar("WhiteBox.jar");
        String bootClassPath = "-Xbootclasspath/a:" + whiteBoxJar;

        OutputAnalyzer dumpOutput = TestCommon.dump(appJar,
                TestCommon.list("PreserveEnumTest",
                                "PreserveEnumTest$PreserveEnumApp"),
                "-Xlog:cds+heap=trace,preinit",
                bootClassPath);
        TestCommon.checkDump(dumpOutput, "Loading classes to share");

        OutputAnalyzer execOutput = TestCommon.exec(appJar,
            "-Xlog:preinit", bootClassPath,
            "-XX:+UnlockDiagnosticVMOptions", "-XX:+WhiteBoxAPI",
            "PreserveEnumTest$PreserveEnumApp");
        TestCommon.checkExec(execOutput, "OK");
    }

    static class PreserveEnumApp {
        public static void main(String[] args) {
            WhiteBox wb = WhiteBox.getWhiteBox();
            // StandardOpenOption is an enum type and is pre-initialized at
            // dump time. The constants defined in StandardOpenOption are
            // pre-populated and archived in the Java heap (shared Java
            // objects) at dump time. Those objects are memory mapped into
            // the runtime Java heap and can be used by the JVM directly once
            // 'materialized' without recreating them by executing related
            // Java code. Test if all constants in StandardOpenOption are
            // shared objects at runtime.
            for (StandardOpenOption p : StandardOpenOption.values()) {
                if (!wb.isShared(p)) {
                    throw new RuntimeException(
                        "StandardOpenOption." + p + " is not shared");
                }
            }
            System.out.println("OK");
        }
    }
}

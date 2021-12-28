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
 * @test PreserveStaticFieldTest
 * @summary Class with static field annotated using Preserve is partially pre-initialized.
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 *          java.base/jdk.internal.misc
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm PreserveStaticFieldTest
 */

import jdk.internal.misc.JavaNetURLAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;

public class PreserveStaticFieldTest {

    private static final String[] preservedStaticFields = {
        "Found @Preserve annotated field jdk/internal/misc/Unsafe.theUnsafe",
        "Found @Preserve annotated field jdk/internal/misc/SharedSecrets.javaNetURLAccess",
        "Archived field jdk/internal/misc/Unsafe::theUnsafe",
        "Archived field jdk/internal/misc/SharedSecrets::javaNetURLAccess",
    };

    private static final String[] partialPreInitializedClasses = {
        "jdk.internal.misc.Unsafe.*is partially pre-initialized",
        "jdk.internal.misc.SharedSecrets.*is partially pre-initialized",
    };

    public static void main(String[] args) throws Exception {
        JarBuilder.build("preserveStaticField",
                         "PreserveStaticFieldTest",
                         "PreserveStaticFieldTest$PreserveStaticFieldApp");

        String appJar = TestCommon.getTestJar("preserveStaticField.jar");
        JarBuilder.build(true, "WhiteBox", "sun/hotspot/WhiteBox");
        String whiteBoxJar = TestCommon.getTestJar("WhiteBox.jar");
        String bootClassPath = "-Xbootclasspath/a:" + whiteBoxJar;

        OutputAnalyzer dumpOutput = TestCommon.dump(appJar,
                TestCommon.list("PreserveStaticFieldTest",
                                "PreserveStaticFieldTest$PreserveStaticFieldApp"),
                "-Xlog:cds+heap=trace,preinit",
                bootClassPath);
        TestCommon.checkDump(dumpOutput, "Loading classes to share");

        for (String f : preservedStaticFields) {
            dumpOutput.shouldMatch(f);
        }

        OutputAnalyzer execOutput = TestCommon.exec(appJar,
            "-Xlog:preinit", bootClassPath,
            "-XX:+UnlockDiagnosticVMOptions", "-XX:+WhiteBoxAPI",
            "--add-exports=java.base/jdk.internal.misc=ALL-UNNAMED",
            "PreserveStaticFieldTest$PreserveStaticFieldApp");
        TestCommon.checkExec(execOutput, "OK");

        for (String c : partialPreInitializedClasses) {
            execOutput.shouldMatch(c);
        }
    }

    static class PreserveStaticFieldApp {
        public static void main(String[] args) {
            JavaNetURLAccess jnua = SharedSecrets.getJavaNetURLAccess();
            WhiteBox wb = WhiteBox.getWhiteBox();
            if (wb.isShared(jnua)) {
                System.out.println("OK");
            } else {
                throw new RuntimeException(
                    "object from SharedSecrets.getJavaNetURLAccess() is not shared");
            }
        }
    }
}

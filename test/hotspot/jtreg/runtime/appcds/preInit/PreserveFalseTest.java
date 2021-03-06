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
 * @test PreserveFalseTest
 * @summary Classes annotated with Preserve(false) must not be pre-initialized.
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 * @compile ../test-classes/Hello.java
 * @run main/othervm PreserveFalseTest
 */

import jdk.test.lib.process.OutputAnalyzer;

public class PreserveFalseTest {

    private static final String[] notPreInitializedClasses = {
        "java.lang.Object",
        "java.lang.Class",
        "java.lang.ClassLoader",
        "java.lang.System",
        "java.lang.Thread",
        "java.lang.ThreadLocal",
        "java.util.ImmutableCollections",
        "jdk.internal.module.ModuleBootstrap",
        "jdk.internal.misc.VM",
    };

    public static void main(String[] args) throws Exception {
        String appJar = JarBuilder.getOrCreateHelloJar();

        OutputAnalyzer dumpOutput = TestCommon.dump(
                appJar, new String[] {"Hello"});
        TestCommon.checkDump(dumpOutput, "Loading classes to share");

        OutputAnalyzer execOutput = TestCommon.exec(appJar, "-Xlog:preinit", "Hello");
        TestCommon.checkExec(execOutput, "Hello World");

        for (String p : notPreInitializedClasses) {
            execOutput.shouldMatch(p + " is not pre-initialized");
        }
    }
}

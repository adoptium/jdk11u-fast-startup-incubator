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
 * @test PreInitStatesBasicTest
 * @summary Test class pre-initialization states.
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 * @compile ../test-classes/Hello.java
 * @run main/othervm PreInitStatesBasicTest
 */

import jdk.test.lib.process.OutputAnalyzer;

public class PreInitStatesBasicTest {

    private static final String[] canPreserveStates = {
        "Set can_preserve for class java.io.Serializable.*no <clinit> or static field",
        "Set can_preserve for class java.lang.String.*with @Preserve annotation",
        "Set can_preserve for class java.lang.Integer.*with @Preserve annotation",
        "Set can_preserve for class java.lang.Long.*with @Preserve annotation",
        "Set can_preserve for class java.io.OutputStream.*no <clinit> or static field",
    };

    private static final String[] preservableStates = {
        "Add preservable class java.lang.String.*",
        "Add preservable class java.io.Serializable.*",
        "Add preservable class java.lang.Integer.*",
        "Add preservable class java.lang.Long.*",
        "Add preservable class java.io.OutputStream.*",
    };

    private static final String[] dumpTimePreInitStates = {
        "Set java.io.Serializable to is_pre_initialized_without_dependency_class",
        "Set java.lang.String to is_pre_initialized_with_dependency_class",
        "Set java.util.Iterator to is_pre_initialized_without_dependency_class",
    };

    private static final String[] runtimePreInitStates = {
        "initializing java.lang.String from archived subgraph",
        "java.lang.String is fully pre-initialized",
        "initializing java.lang.Integer\\$IntegerCache from archived subgraph",
        "java.lang.Integer\\$IntegerCache.*is partially pre-initialized",
    };

    public static void main(String[] args) throws Exception {
        String appJar = JarBuilder.getOrCreateHelloJar();

        // Dump time tests
        OutputAnalyzer dumpOutput = TestCommon.dump(
                appJar, new String[] {"Hello"}, "-Xlog:preinit");
        TestCommon.checkDump(dumpOutput, "Loading classes to share");

        for (String canPreservePattern : canPreserveStates) {
            dumpOutput.shouldMatch(canPreservePattern);
        }

        for (String preservablePattern : preservableStates) {
            dumpOutput.shouldMatch(preservablePattern);
        }

        for (String dumpTimePreInitPattern : dumpTimePreInitStates) {
            dumpOutput.shouldMatch(dumpTimePreInitPattern);
        }

        // Runtime tests
        OutputAnalyzer execOutput = TestCommon.exec(appJar, "-Xlog:preinit", "Hello");
        TestCommon.checkExec(execOutput, "Hello World");

        for (String runtimePreInitPattern : runtimePreInitStates) {
            execOutput.shouldMatch(runtimePreInitPattern);
        }
    }
}

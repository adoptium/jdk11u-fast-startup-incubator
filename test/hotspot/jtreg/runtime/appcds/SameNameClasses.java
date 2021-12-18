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
 * @summary Test same named classes in both -cp and -Xbootclasspath/a. The
 *          class in the -Xbootclasspath/a should be loaded.
 * @requires vm.cds
 * @library /test/lib ..
 * @modules java.base/jdk.internal.misc
 *          java.management
 * @run main SameNameClasses
 */

import java.nio.file.Path;
import java.nio.file.Paths;
import jdk.test.lib.compiler.InMemoryJavaCompiler;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.util.JarUtils;

public class SameNameClasses {
    private final static String TEST_APP = "SameNameClassesTestApp";
    private final static String TEST_CLASSES = System.getProperty("test.classes");
    private final static String CP_DIR = TEST_CLASSES + "/CP/";
    private final static Path CP_JAR_PATH = Paths.get(CP_DIR, "cp.jar");
    private final static String BOOT_APPEND_DIR = TEST_CLASSES + "/boot_append/";
    private final static Path BOOT_APPEND_JAR_PATH = Paths.get(CP_DIR, "boot_append.jar");

    static String create_jar(String code, String dir, Path jar) throws Throwable {
        String source = "public class " + TEST_APP + " { " +
                        "    public static void main(String args[]) { " +
                             code       +
                        "    } "        +
                        "}";
        ClassFileInstaller.writeClassToDisk(
            TEST_APP, InMemoryJavaCompiler.compile(TEST_APP, source), dir);

        JarUtils.createJarFile(jar,
                               Paths.get(dir),
                               Paths.get(dir, TEST_APP + ".class"));
        return jar.toString();
    }

    public static void main(String[] args) throws Throwable {
        String cp_jar_path = create_jar(
                   "System.out.println(\"From classpath!\");",
                   CP_DIR, CP_JAR_PATH);
        String boot_append_jar_path = create_jar(
                   "System.out.println(\"From bootclasspath append!\");",
                   BOOT_APPEND_DIR, BOOT_APPEND_JAR_PATH);

        String[] appClasses = {TEST_APP};
        OutputAnalyzer output = TestCommon.testDump(cp_jar_path, appClasses,
                                     "-Xbootclasspath/a:" + boot_append_jar_path,
                                     "-Xlog:class+load");
        output.shouldMatch("SameNameClassesTestApp source: .*boot_append.jar");

        TestCommon.run(
            "-cp", cp_jar_path,
            "-Xbootclasspath/a:" + boot_append_jar_path,
            TEST_APP)
                .assertNormalExit("From bootclasspath append");
    }
}

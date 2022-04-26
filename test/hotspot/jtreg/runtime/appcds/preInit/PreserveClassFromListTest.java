/*
 * Copyright (c) 2022 Alibaba Group Holding Limited. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation. Alibaba designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*
 * @test PreserveClassFromListTest
 * @summary Pre-initialize classes from given class list file
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 *          java.base/jdk.internal.misc
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm PreserveClassFromListTest
 */

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

import jdk.internal.misc.JavaNetURLAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;

public class PreserveClassFromListTest {

    private static final String[] dumpContent = {
        "Add preservable class PreserveClassFromListTest\\$PreserveStaticFieldApp",
        "Archived object klass PreserveClassFromListTest\\$PreserveStaticFieldApp.*=> java.util.HashMap"
    };
    private static final String[] runContent = {
        "initializing PreserveClassFromListTest\\$PreserveStaticFieldApp from archived subgraph",
        "PreserveClassFromListTest\\$PreserveStaticFieldApp is fully pre-initialized"
    };

    public static void main(String[] args) throws Exception {
        JarBuilder.build("preserveClassFromList",
                         "PreserveClassFromListTest",
                         "PreserveClassFromListTest$PreserveStaticFieldApp");

        String appJar = TestCommon.getTestJar("preserveClassFromList.jar");
        Path filePath = Path.of("preservable.list");
        try {
            Files.writeString(filePath, "PreserveClassFromListTest$PreserveStaticFieldApp\nInvalidClass");
        } catch (IOException e) {
            e.printStackTrace();
        }

        OutputAnalyzer dumpOutput = TestCommon.dump(appJar,
                TestCommon.list("PreserveClassFromListTest",
                                "PreserveClassFromListTest$PreserveStaticFieldApp"),
                "-Xlog:cds+heap=trace,preinit",
                "-XX:PreInitializeArchivedClassList=" + filePath.toAbsolutePath().toString());
        TestCommon.checkDump(dumpOutput, "Loading classes to share");
        TestCommon.checkDump(dumpOutput, "Failed to load klass InvalidClass");
        for (String f : dumpContent) {
            dumpOutput.shouldMatch(f);
        }

        OutputAnalyzer execOutput = TestCommon.exec(appJar,
            "-Xlog:preinit",
            "PreserveClassFromListTest$PreserveStaticFieldApp");
        TestCommon.checkExec(execOutput, "42");
        TestCommon.checkExec(execOutput, "1001");
        TestCommon.checkExec(execOutput, "B");
        for (String f : runContent) {
            execOutput.shouldMatch(f);
        }
    }

    static class PreserveStaticFieldApp {
        private static Map<Integer, Integer> map = new HashMap<>();

        private static char ch = 'B';

        static {
            for (int i = 0; i < 1024; i++) {
                map.put(i, i);
            }
        }
        public static void main(String[] args) {
            System.out.println(map.get(42));
            System.out.println(map.get(1001));
            System.out.println(ch);
        }
    }
}

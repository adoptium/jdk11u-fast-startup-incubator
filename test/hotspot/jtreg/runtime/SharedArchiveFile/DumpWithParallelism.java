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

/**
 * @test
 * @summary Testing CDS dumping with DumpWithParallelism.
 * @requires vm.cds
 * @library /test/lib
 * @modules java.base/jdk.internal.misc
 *          java.management
 * @run main DumpWithParallelism
 */

import jdk.test.lib.cds.CDSTestUtils;
import jdk.test.lib.process.ProcessTools;
import jdk.test.lib.process.OutputAnalyzer;

public class DumpWithParallelism {
    public static void main(String[] args) throws Exception {
        for (int i = 1; i <= 32; i *= 2) {
            test(i);
        }
    }

    static void test(int parallelism) throws Exception {
        String jsa_name = "./DumpWithParallelism_" + parallelism + ".jsa";
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(true,
                                "-XX:SharedArchiveFile=" + jsa_name,
                                "-XX:DumpWithParallelism=" + parallelism,
                                "-Xshare:dump");
        OutputAnalyzer out = CDSTestUtils.executeAndLog(pb, "dump");
        CDSTestUtils.checkDump(out);

        pb = ProcessTools.createJavaProcessBuilder(true,
                "-XX:SharedArchiveFile=" + jsa_name,
                "-Xshare:on", "DumpWithParallelism$RuntimeTest");
        CDSTestUtils.executeAndLog(pb, "exec").shouldHaveExitValue(0);;
    }

    static class RuntimeTest {
        public static void main(String[] args) {
            System.out.println("Runtime test");
        }
    }
}

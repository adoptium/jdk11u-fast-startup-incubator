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
 * @test ConcurrentInitTest
 * @summary Concurrently initialize some shared classes. No crash should occur.
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/appcds
 * @modules jdk.jartool/sun.tools.jar
 * @build sun.hotspot.WhiteBox
 * @run driver ClassFileInstaller sun.hotspot.WhiteBox
 * @run main/othervm ConcurrentInitTest
 */

import java.nio.ByteBuffer;
import java.util.Formatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import jdk.test.lib.process.OutputAnalyzer;
import sun.hotspot.WhiteBox;

// This test simulates a case where multiple threads are trying to
// intialize some shared classes at the same time and serves as a
// regression test.
//
// The test creates a specified number of threads and tries to
// trigger the class initialization for ByteBuffer and Formatter
// simultaneously from different threads. The test should run to
// completion without causing any crashes.
public class ConcurrentInitTest {
    static final int NUM_MIN_THREADS = 2;
    static final int NUM_MAX_THREADS = 16;
    public static void main(String[] args) throws Exception {
        JarBuilder.build("concurrentInit",
                         "ConcurrentInitTest",
                         "ConcurrentInitTest$ConcurrentInitRunnable");

        String appJar = TestCommon.getTestJar("concurrentInit.jar");
        JarBuilder.build(true, "WhiteBox", "sun/hotspot/WhiteBox");
        String whiteBoxJar = TestCommon.getTestJar("WhiteBox.jar");
        String bootClassPath = "-Xbootclasspath/a:" + whiteBoxJar;

        OutputAnalyzer dumpOutput = TestCommon.dump(appJar,
                // ByteBuffer and Formatter should be already in the default
                // classlist. Explicitly listing them here just to be sure.
                TestCommon.list("java/nio/ByteBuffer",
                                "java/util/Formatter",
                                "ConcurrentInitTest",
                                "ConcurrentInitTest$ConcurrentInitRunnable"),
                bootClassPath);
        TestCommon.checkDump(dumpOutput, "Loading classes to share");

        int bound = NUM_MAX_THREADS - NUM_MIN_THREADS + 1;
        int numThreads = (new Random()).nextInt(bound) + NUM_MIN_THREADS;
        String[] classNames = {"java.nio.ByteBuffer",
                               "java.util.Formatter"};
        for (String s : classNames) {
            System.out.println(
                "Testing concurrently initializing " + s + " with " +
                numThreads + " threads ...");
            OutputAnalyzer execOutput = TestCommon.exec(
                appJar, bootClassPath, "-XX:+UnlockDiagnosticVMOptions",
                "-XX:+WhiteBoxAPI", "ConcurrentInitTest$ConcurrentInitRunnable",
                Integer.toString(numThreads), s);
            TestCommon.checkExec(execOutput, "OK");
        }
    }

    static class ConcurrentInitRunnable implements Runnable {
        static int numThreads;
        static String className;

        static Object lock = new Object();
        static Class initializedClass;

        String runnableName;

        ConcurrentInitRunnable(String name) {
            this.runnableName = name;
        }

        @Override
        public void run() {
            Class c;
            // Multiple threads try to initialize the class at the same time.
            try {
                c = Class.forName(className, true, null);
            } catch (ClassNotFoundException cnfe) {
                throw new RuntimeException(
                    className + " initialization failed");
            }

            synchronized(lock) {
                if (initializedClass == null) {
                    initializedClass = c;
                    System.out.println(runnableName + " initialized " + className);
                }
            }
        }

        static void concurrentTest() {
            List<Thread> threads = new ArrayList<Thread>();
            for (int i = 0; i < numThreads; i++) {
                threads.add(new Thread(
                    new ConcurrentInitRunnable("ConcurrentInitRunnable_"+i)));
            }

            // Using a separate loop from above may help stress the concurrency.
            for (Thread t1 : threads) {
                t1.start();
            }

            for (Thread t2 : threads) {
                try {
                    t2.join();
                } catch (InterruptedException ie) {
                    throw new AssertionError(ie);
                }
            }
        }

        static void checkClass() {
            WhiteBox wb = WhiteBox.getWhiteBox();
            if (!wb.isSharedClass(initializedClass)) {
                throw new RuntimeException(
                    className + " is not shared");
            }
        }

        // Test should complete successfully without any crashes.
        public static void main(String[] args) {
            numThreads = Integer.parseInt(args[0]);
            className = args[1];

            // Do concurrentTest() before checkClass(). This is to
            // avoid unintentionally triggering the initialization of the
            // class too early due to executing additional Java code.
            concurrentTest();

            // Now check if the class is a shared class.
            checkClass();

            System.out.println("OK");
        }
    }
}

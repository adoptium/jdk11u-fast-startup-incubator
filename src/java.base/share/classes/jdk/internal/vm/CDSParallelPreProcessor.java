/*
 * Copyright 2020 Google, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  This particular file is
 * subject to the "Classpath" exception as provided in the LICENSE file
 * that accompanied this code.
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
package jdk.internal.vm;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Support loading and pre-processing classes in parallel at CDS dump time.
 *
 * CDS dumping involves a parallel phase and a non-parallel phase. The static
 * CDSParallelPreProcessor.preLoadAndProcess() method is invoked by the VM to
 * enter the parallel phase.
 *
 * During the parallel phase, the classlist is split into a number of sublists
 * according to DumpWithParallelism value and processed parallelly in different
 * tasks. Classes on the list are loaded but not explicitly initialized, and
 * then pre-processed. Pre-processing classes during the parallel phase includes
 * linking class, verifying class (when required), and rewriting bytecodes.
 * After all parallel tasks complete, the control is transferred back to the VM,
 * which then enters the non-parallel phase to further process the loaded data
 * and create the archive file.
 */
public class CDSParallelPreProcessor {

    // Total number of loaded classes.
    private static AtomicInteger classCount = new AtomicInteger(0);

    static void updateClassCount(int num) {
        classCount.getAndAdd(num);
    }

    private static class SubListProcessor implements Runnable {
        List<String> classList;
        SubListProcessor(List<String> list) {
            classList = list;
        }

        @Override
        public void run() {
            updateClassCount(processSubList(classList));
        }
    }

    // Read the 'classListFile' and split the list into a number of sublists
    // according to the value of 'parallelism' argument (determined by
    // the VM DumpWithParallelism flag).
    //
    // The sublists are handled parallelly to load and pre-process classes.
    // Wait for all worker threads to complete and collect the total number
    // of loaded classes.
    public static int preLoadAndProcess(String classListFile,
                                        int parallelism) throws Throwable {
        // Potentially this could be implemented using ExecutorService. However
        // the thread management in ExecutorService is opaque to us. The
        // pooled threads may not stop immediately when shutdown() or
        // shutdownNow() is called and may be interrupted at a later point.
        // To avoid any unwanted side-effects, which include concurrently
        // loading any new classes due to executing Java code after this method
        // is returned, an approach using direct thread management is chosen
        // here.
        List<List<String>> subLists = getSortedAndSplitClassList(
                Files.readAllLines(Paths.get(classListFile)), parallelism);
        Thread[] workers = new Thread[parallelism];
        int num = 0;
        for (List<String> list : subLists) {
            workers[num] = new Thread(new SubListProcessor(list));
            workers[num].start();
            num++;
        }
        for (int i = 0; i < num; i++) {
            workers[i].join();
        }
        return classCount.get();
    }

    /*
     * Google: This method is a duplicate of FrameworkLoader.java.
     */
    static List<List<String>> getSortedAndSplitClassList(List<String> classesToLoad,
                                                         int splitCount) {
        Collections.sort(classesToLoad);

        List<List<String>> returnList = new ArrayList<>(splitCount);
        // The loop is index based so List.subList can be used easily.
        int nextIndex = 0;
        int remainingThreads = splitCount;
        int totalClassCount = classesToLoad.size();
        int remainingClasses = totalClassCount - nextIndex;
        for (;
            remainingClasses > 0 && remainingThreads > 0;
            remainingClasses = totalClassCount - nextIndex) {
          int sublistStartIndex = nextIndex;
          int splitSize = (int) Math.max(1, remainingClasses / (double) remainingThreads);
          nextIndex += splitSize;
          // Find the index of the next non-inner class.
          for (;
              nextIndex < totalClassCount && classesToLoad.get(nextIndex).indexOf('$') != -1;
              nextIndex++) {}
          List<String> subList = classesToLoad.subList(sublistStartIndex, nextIndex);
          returnList.add(subList);
          remainingThreads--;
        }
        return returnList;
    }

    // The 'subList' is a segment of the original classlist that's produced by
    // getSortedAndSplitClassList() method. Lines start with '#' are comments
    // and are ignored. Lines start with '[' are treated as array and are not
    // supported for classlist.
    //
    // For each class entry in the 'subList', load the class without
    // initializing, and pre-process the loaded class. Pre-processing includes
    // linking the class, verifying the the class if required and rewriting the
    // bytecodes. It is not a fatal error if a class in the classlist cannot be
    // found. A 'Preload Warning' is printed out in that case.
    //
    // The number of successfully loaded classes is tracked and returned.
    static int processSubList(List<String> subList) {
        int classNum = 0;
        for (String name : subList) {
            if (name.startsWith("#")) {
                // Line starts with '#' is treated as comment and is ignored.
            } else if (name.startsWith("[")) {
                // Array is not support in classlist.
                System.out.println("Preload Warning: Cannot find " + name);
            } else {
                String qualified_name = name.replace('/', '.');
                Class<?> c = null;
                try {
                    // First try loading the requested class by calling the
                    // system class loader's loadClass(). That handles all
                    // following cases:
                    //
                    // - classes in the runtime modules image
                    // - application classes with unnamed modules from -cp path
                    // - application classes with named module from the module
                    //   path
                    // - unnamed module classes from -Xbootclasspath/a:
                    c = ClassLoader.getSystemClassLoader().loadClass(qualified_name);
                } catch (Throwable t) {
                    // Ignore any exception
                }

                try {
                    if (c == null) {
                      // Additional classes with existing named modules defined
                      // in the runtime modules image can be loaded from
                      // -Xbootclasspath/a. That case is not handled by system
                      // class loader's loadClass(). Call Class.forName() using
                      // the null class loader explicitly to handle that.
                      c = Class.forName(qualified_name, false, null);
                    }
                    preProcessClass(c);
                    classNum ++;
                } catch (ClassNotFoundException | NoClassDefFoundError ex) {
                    System.out.println("Preload Warning: Cannot find " + name);
                } catch (UnsupportedClassVersionError err) {
                    // Error is already reported by the VM
                }
            }
        }
        return classNum;
    }

    private static native void preProcessClass(Class<?> c);
}

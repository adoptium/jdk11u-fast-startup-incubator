## Goal
This is a repository for collaboratively exploring Java startup time optimization ideas. 
The intent is to contribute the resulting work into OpenJDK projects by their original 
authors. Alternatively, resulting work may be moved into a new Eclipse Adoptium project. Please
see [Eclipse Adoptium Incubator](https://projects.eclipse.org/projects/adoptium.incubator)
for additional information.

Areas of current interest include (but not limited to):
 * General class pre-initialization
 * A static analyzer for identifying unsafe class pre-initialization
 * Use ahead-of-time (AOT) compilation and generate more efficient code

## JDK Release
We have prepared a pipeline to build/test/package for fast-startup-incubator project. It automatically fetches the latest code and builds a JDK artifact for this project. You could find it from http://ci.dragonwell-jdk.io/job/build-scripts/job/jobs/job/jdk11u/job/jdk11u-linux-x64-fast_startup/ . All released JDK artifacts can also be found there.

## Goal
This is a repository for collaboratively exploring Java startup time optimization ideas. 
The intent is to contribute the resulting work into OpenJDK projects by their original 
authors. Alternatively, resulting work may be moved into a new Eclipse Adoptium project. Please
see [Eclipse Adoptium Incubator](https://projects.eclipse.org/projects/adoptium.incubator)
for additional information.

Areas of current interest include (but not limited to):
 * General class pre-initialization
 * Static analyzer for identifying unsafe pre-initialization
 * AOT

## Development
PR plays the main role in our development. Any PR should be properly reviewed (at least ONE reviewer, probably more for non-trivial patches) before merging. We expect to have an automatic CI process in the future. Whenever a PR is submitted, it carries out the simplest build (even tier1 tests).

## Release cycle
We propose to follow OpenJDK 11u upstream release cycle. The last release was in May, so we expect the next preview release will be in August. During this period, the first milestone we propose is as follows.

## Milestone
We have drafted the first feasible milestone. Please feel free to add milestones that not documented on this page if you think it's indeed necessary! You can also remove/edit it if you think it is not clear.
+ General class pre-initialization
  - Preliminary Backports for future general pre-initialization work. (@Google + @Alibaba)
  - Apply @jiangli's [initial prototype](http://cr.openjdk.java.net/~jiangli/Leyden/general_class_pre_initialization_core_mechanism/general_class_pre_init_patch_1) and additional changes & enhancements based on 2). (@Google + @Alibaba)
  - Moreover, support pre-initializing classes which are specified by users via a class list file. This provides the functionality of specifying a class list of safe classes, it works as starting points for future static analyzer tool. (@Alibaba)
+ A static analyzer for identifying unsafe classes that can not be used for pre-initialization(@Alibaba)
  - Feed a class list(-XX:DumpedLoadedClassList) to the static analyser, whose <clinit>() methods are entry points of pre-initialisation safety analysis
  - Based on pre-initialisation safety analysis, generate safe class list for VM side.
+ AOT(@Alibaba)
  - Leveraging class pre-initialization to: 
  - optimize static calls
  - remove redundant class initialization check

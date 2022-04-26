/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "classfile/classLoaderData.inline.hpp"
#include "classfile/javaClasses.inline.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/vmSymbols.hpp"
#include "logging/log.hpp"
#include "logging/logMessage.hpp"
#include "logging/logStream.hpp"
#include "memory/filemap.hpp"
#include "memory/heapShared.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/metadataFactory.hpp"
#include "memory/metaspaceClosure.hpp"
#include "memory/resourceArea.hpp"
#include "oops/compressedOops.inline.hpp"
#include "oops/fieldStreams.hpp"
#include "oops/oop.inline.hpp"
#include "oops/markOop.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "runtime/safepointVerifiers.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/ostream.hpp"
#if INCLUDE_G1GC
#include "gc/g1/g1CollectedHeap.hpp"
#endif

#if INCLUDE_CDS_JAVA_HEAP

bool HeapShared::_closed_archive_heap_region_mapped = false;
bool HeapShared::_open_archive_heap_region_mapped = false;
bool HeapShared::_archive_heap_region_fixed = false;

address   HeapShared::_narrow_oop_base;
int       HeapShared::_narrow_oop_shift;

/*
   Google: No longer needed with the new general class pre-initialization
           support, which is more developer friendly and easier to use.
//
// If you add new entries to the following tables, you should know what you're doing!
//

// Entry fields for shareable subgraphs archived in the closed archive heap
// region. Warning: Objects in the subgraphs should not have reference fields
// assigned at runtime.
static ArchivableStaticFieldInfo closed_archive_subgraph_entry_fields[] = {
  {"java/lang/Integer$IntegerCache",           "archivedCache"},
  {"java/lang/Long$LongCache",                 "archivedCache"},
  {"java/lang/Byte$ByteCache",                 "archivedCache"},
  {"java/lang/Short$ShortCache",               "archivedCache"},
  {"java/lang/Character$CharacterCache",       "archivedCache"},
};
// Entry fields for subgraphs archived in the open archive heap region.
static ArchivableStaticFieldInfo open_archive_subgraph_entry_fields[] = {
  {"jdk/internal/module/ArchivedModuleGraph",  "archivedSystemModules"},
  {"jdk/internal/module/ArchivedModuleGraph",  "archivedModuleFinder"},
  {"jdk/internal/module/ArchivedModuleGraph",  "archivedMainModule"},
  {"jdk/internal/module/ArchivedModuleGraph",  "archivedConfiguration"},
  {"java/util/ImmutableCollections$ListN",     "EMPTY_LIST"},
  {"java/util/ImmutableCollections$MapN",      "EMPTY_MAP"},
  {"java/util/ImmutableCollections$SetN",      "EMPTY_SET"},
  {"java/lang/module/Configuration",           "EMPTY_CONFIGURATION"},
};

const static int num_closed_archive_subgraph_entry_fields =
  sizeof(closed_archive_subgraph_entry_fields) / sizeof(ArchivableStaticFieldInfo);
const static int num_open_archive_subgraph_entry_fields =
  sizeof(open_archive_subgraph_entry_fields) / sizeof(ArchivableStaticFieldInfo);
*/

////////////////////////////////////////////////////////////////
//
// Java heap object archiving support
//
////////////////////////////////////////////////////////////////
void HeapShared::fixup_mapped_heap_regions() {
  FileMapInfo *mapinfo = FileMapInfo::current_info();
  mapinfo->fixup_mapped_heap_regions();
  set_archive_heap_region_fixed();
}

unsigned HeapShared::oop_hash(oop const& p) {
  assert(!p->mark()->has_bias_pattern(),
         "this object should never have been locked");  // so identity_hash won't safepoin
  unsigned hash = (unsigned)p->identity_hash();
  return hash;
}

HeapShared::ArchivedObjectCache* HeapShared::_archived_object_cache = NULL;
oop HeapShared::find_archived_heap_object(oop obj) {
  assert(DumpSharedSpaces, "dump-time only");
  ArchivedObjectCache* cache = archived_object_cache();
  oop* p = cache->get(obj);
  if (p != NULL) {
    return *p;
  } else {
    return NULL;
  }
}

oop HeapShared::archive_heap_object(oop obj, Thread* THREAD) {
  assert(DumpSharedSpaces, "dump-time only");

  oop ao = find_archived_heap_object(obj);
  if (ao != NULL) {
    // already archived
    return ao;
  }

  int len = obj->size();
  if (G1CollectedHeap::heap()->is_archive_alloc_too_large(len)) {
    log_debug(cds, heap)("Cannot archive, object (" PTR_FORMAT ") is too large: " SIZE_FORMAT,
                         p2i(obj), (size_t)obj->size());
    return NULL;
  }

  // Pre-compute object identity hash at CDS dump time.
  obj->identity_hash();

  oop archived_oop = (oop)G1CollectedHeap::heap()->archive_mem_allocate(len);
  if (archived_oop != NULL) {
    Copy::aligned_disjoint_words((HeapWord*)obj, (HeapWord*)archived_oop, len);
    MetaspaceShared::relocate_klass_ptr(archived_oop);

    // Reset markOop and retain the pre-computed identity hash.
    archived_oop->set_mark_raw(
      markOopDesc::prototype()->copy_set_hash(obj->identity_hash()));

    ArchivedObjectCache* cache = archived_object_cache();
    cache->put(obj, archived_oop);
    log_debug(cds, heap)("Archived heap object " PTR_FORMAT " ==> " PTR_FORMAT,
                         p2i(obj), p2i(archived_oop));
    if (log_is_enabled(Trace, cds, heap)) {
      LogTarget(Trace, cds, heap) log;
      LogStream out(log);
      obj->print_on(&out);
    }
  } else {
    log_error(cds, heap)(
      "Cannot allocate space for object " PTR_FORMAT " in archived heap region",
      p2i(obj));
    vm_exit(1);
  }
  return archived_oop;
}

oop HeapShared::materialize_archived_object(narrowOop v) {
  assert(archive_heap_region_fixed(),
         "must be called after archive heap regions are fixed");
  if (!CompressedOops::is_null(v)) {
    oop obj = HeapShared::decode_from_archive(v);
    return G1CollectedHeap::heap()->materialize_archived_object(obj);
  }
  return NULL;
}

void HeapShared::archive_klass_objects(Thread* THREAD) {
  GrowableArray<Klass*>* klasses = MetaspaceShared::collected_klasses();
  assert(klasses != NULL, "sanity");
  for (int i = 0; i < klasses->length(); i++) {
    Klass* k = klasses->at(i);

    // archive mirror object
    java_lang_Class::archive_mirror(k, CHECK);

    // archive the resolved_referenes array
    if (k->is_instance_klass()) {
      InstanceKlass* ik = InstanceKlass::cast(k);
      ik->constants()->archive_resolved_references(THREAD);
    }
  }
}

void HeapShared::archive_java_heap_objects(GrowableArray<MemRegion> *closed,
                                           GrowableArray<MemRegion> *open) {
  if (!is_heap_object_archiving_allowed()) {
    if (log_is_enabled(Info, cds)) {
      log_info(cds)(
        "Archived java heap is not supported as UseHeapObjectArchiving, "
        "UseG1GC, UseCompressedOops and UseCompressedClassPointers are "
        "required. Current settings: UseHeapObjectArchiving=%s, UseG1GC=%s, "
        "UseCompressedOops=%s, UseCompressedClassPointers=%s.",
        BOOL_TO_STR(UseHeapObjectArchiving),BOOL_TO_STR(UseG1GC),
        BOOL_TO_STR(UseCompressedOops),
        BOOL_TO_STR(UseCompressedClassPointers));
    }
    return;
  }

  // GOOGLE: G1HeapVerifier::verify_ready_for_archiving is not useful as it
  //         starts from the lowest region and iterates upward to check if
  //         there are allocated regions among the free regions.
  //         VerifyReadyForArchivingRegionClosure::do_heap_region reports
  //         'unexpected hole' if any non-humongous region is found after any
  //         free regions. The G1ArchiveAllocator::alloc_new_region allocates
  //         regions starting from the highest free region. The lower region
  //         status does not affect archive regions allocation at all.
  //         HeapShared::archive_heap_object ensures the consecutive archive
  //         heap regions must be able to accommodate all archived heap objects.
  //         Otherwise, it reports error and aborts the JVM.
  //G1HeapVerifier::verify_ready_for_archiving();

  {
    NoSafepointVerifier nsv;

    // Cache for recording where the archived objects are copied to
    create_archived_object_cache();

    tty->print_cr("Dumping objects to closed archive heap region ...");
    NOT_PRODUCT(StringTable::verify());
    copy_closed_archive_heap_objects(closed);

    tty->print_cr("Dumping objects to open archive heap region ...");
    copy_open_archive_heap_objects(open);

    destroy_archived_object_cache();
  }

  G1HeapVerifier::verify_archive_regions();
}

void HeapShared::copy_closed_archive_heap_objects(
                                    GrowableArray<MemRegion> * closed_archive) {
  assert(is_heap_object_archiving_allowed(), "Cannot archive java heap objects");

  Thread* THREAD = Thread::current();
  G1CollectedHeap::heap()->begin_archive_alloc_range();

  // Archive interned string objects
  StringTable::write_to_archive();

  // GOOGLE: All subgraph archiving are done in the open archive heap region
  //         currently with the general class pre-initialization support.
  //         For upstream, supporting archiving 'immutable' subgraphs in closed
  //         archive heap region for memory sharing is desirable. Internally,
  //         we also want to understand and evaluate the benefit of memory
  //         sharing on Borg enviornment.
  //
  //archive_object_subgraphs(closed_archive_subgraph_entry_fields,
  //                         num_closed_archive_subgraph_entry_fields,
  //                         true /* is_closed_archive */, THREAD);

  G1CollectedHeap::heap()->end_archive_alloc_range(closed_archive,
                                                   os::vm_allocation_granularity());
}

void HeapShared::copy_open_archive_heap_objects(
                                    GrowableArray<MemRegion> * open_archive) {
  assert(is_heap_object_archiving_allowed(), "Cannot archive java heap objects");

  Thread* THREAD = Thread::current();
  G1CollectedHeap::heap()->begin_archive_alloc_range(true /* open */);

  // Archive primitive type mirrors.
  java_lang_Class::archive_basic_type_mirrors(THREAD);

  // Archive mirrors, constant pool resolved_references arrays, etc.
  archive_klass_objects(THREAD);

  if (PreInitializeArchivedClass) {
    // Check object subgraphs referenced from the static fields.
    check_preservable_klasses_and_fields(THREAD);

    // TODO(b/168841205): All preservable static fields' object subgraphs are
    // copied to the open archive heap regions currently. It probably worth
    // supporting archiving using the closed archive heap regions for memory
    // sharing. That would be desirable for upstream.

    // Archive all individual static fields that are annotated with
    // @Preserve.
    archive_preservable_static_field_subgraphs(THREAD);

    // Archive all static fields for classes with @Preserve.
    archive_preservable_klass_static_fields_subgraphs(THREAD);
  }

  G1CollectedHeap::heap()->end_archive_alloc_range(open_archive,
                                                   os::vm_allocation_granularity());
}

void HeapShared::init_narrow_oop_decoding(address base, int shift) {
  _narrow_oop_base = base;
  _narrow_oop_shift = shift;
}

//
// Subgraph archiving support
//
HeapShared::DumpTimeKlassSubGraphInfoTable* HeapShared::_dump_time_subgraph_info_table = NULL;
HeapShared::RunTimeKlassSubGraphInfoTable   HeapShared::_run_time_subgraph_info_table;

// Get the subgraph_info for Klass k. A new subgraph_info is created if
// there is no existing one for k. The subgraph_info records the relocated
// Klass* of the original k.
KlassSubGraphInfo* HeapShared::get_subgraph_info(Klass* k,
                                                 bool is_partial_pre_init) {
  Klass* relocated_k = MetaspaceShared::get_relocated_klass(k);
  KlassSubGraphInfo* info = find_subgraph_info(relocated_k);
  if (info == NULL) {
    _dump_time_subgraph_info_table->put(
      relocated_k, KlassSubGraphInfo(relocated_k, is_partial_pre_init));
    info = _dump_time_subgraph_info_table->get(relocated_k);
    ++ _dump_time_subgraph_info_table->_count;
  }
  return info;
}

// Find an existing KlassSubGraphInfo record for a relocated Klass.
KlassSubGraphInfo* HeapShared::find_subgraph_info(Klass* relocated_k) {
  assert(DumpSharedSpaces, "dump time only");
  if (relocated_k == NULL) {
    return NULL;
  }
  KlassSubGraphInfo* info = _dump_time_subgraph_info_table->get(relocated_k);
  return info;
}

// Add an entry field to the current KlassSubGraphInfo.
void KlassSubGraphInfo::add_subgraph_entry_field(
      int static_field_offset, oop v, bool is_closed_archive) {
  assert(DumpSharedSpaces, "dump time only");
  if (_subgraph_entry_fields == NULL) {
    _subgraph_entry_fields =
      new(ResourceObj::C_HEAP, mtClass) GrowableArray<juint>(10, true);
  }
  _subgraph_entry_fields->append((juint)static_field_offset);
  _subgraph_entry_fields->append(CompressedOops::encode(v));
  _subgraph_entry_fields->append(is_closed_archive ? 1 : 0);
}

// Add the Klass* for an object in the current KlassSubGraphInfo's subgraphs.
// Only objects of classes loaded by BUILTIN class loaders can be included in
// sub-graph.
void KlassSubGraphInfo::add_subgraph_object_klass(Klass* orig_k, Klass *relocated_k) {
  assert(DumpSharedSpaces, "dump time only");
  assert(relocated_k == MetaspaceShared::get_relocated_klass(orig_k),
         "must be the relocated Klass in the shared space");

  if (_subgraph_object_klasses == NULL) {
    _subgraph_object_klasses =
      new(ResourceObj::C_HEAP, mtClass) GrowableArray<Klass*>(50, true);
  }

  assert(relocated_k->is_shared(), "must be a shared class");

  if (_k == relocated_k) {
    // Don't add the Klass containing the sub-graph to it's own klass
    // initialization list.
    return;
  }

  if (relocated_k->is_instance_klass()) {
    // Only support shared classes with builtin class loaders and the
    // shared_classpath_index must be >=0. See comments for BUILTIN type
    // in systemDictionaryShared.hpp.
    assert(relocated_k->shared_classpath_index() >= 0,
          "must be BUILTIN type");
    // SystemDictionary::xxx_klass() are not updated, need to check
    // the original Klass*
    if (orig_k == SystemDictionary::String_klass() ||
        orig_k == SystemDictionary::Object_klass() ||
        orig_k == SystemDictionary::Class_klass()  ||
        orig_k == SystemDictionary::Integer_klass()) {
      // Initialized early during VM initialization. No need to be added
      // to the sub-graph object dependency class list.
      return;
    }
  } else if (relocated_k->is_objArray_klass()) {
    Klass* abk = ObjArrayKlass::cast(relocated_k)->bottom_klass();
    if (abk->is_instance_klass()) {
      guarantee(abk->shared_classpath_index() >= 0,
                "must be BUILTIN type");
    }
    if (relocated_k == Universe::objectArrayKlassObj()) {
      // Initialized early during Universe::genesis. No need to be added
      // to the list.
      return;
    }
  } else {
    assert(relocated_k->is_typeArray_klass(), "must be");
    // Primitive type arrays are created early during Universe::genesis.
    return;
  }

  if (log_is_enabled(Debug, cds, heap)) {
    if (!_subgraph_object_klasses->contains(relocated_k)) {
      ResourceMark rm;
      log_debug(cds, heap)("Adding klass %s", orig_k->external_name());
    }
  }

  _subgraph_object_klasses->append_if_missing(relocated_k);
}

// Initialize an archived subgraph_info_record from the given KlassSubGraphInfo.
void ArchivedKlassSubGraphInfoRecord::init(KlassSubGraphInfo* info) {
  _k = info->klass();
  _is_partial_pre_init = info->is_partial_pre_init();
  _entry_field_records = NULL;
  _subgraph_object_klasses = NULL;

  // populate the entry fields
  GrowableArray<juint>* entry_fields = info->subgraph_entry_fields();
  if (entry_fields != NULL) {
    int num_entry_fields = entry_fields->length();
    assert(num_entry_fields % 3 == 0, "sanity");
    _entry_field_records =
      MetaspaceShared::new_ro_array<juint>(num_entry_fields);
    for (int i = 0 ; i < num_entry_fields; i++) {
      _entry_field_records->at_put(i, entry_fields->at(i));
    }
  }

  // Add the Klasses of the objects in the sub-graphs to the dependency list.
  // TODO(b/165809160): The list can be further optimized by removing any
  // klasses that are already included in the current Klass's dependency list.
  GrowableArray<Klass*>* subgraph_object_klasses = info->subgraph_object_klasses();
  if (subgraph_object_klasses != NULL) {
    int num_subgraphs_klasses = subgraph_object_klasses->length();
    _subgraph_object_klasses =
      MetaspaceShared::new_ro_array<Klass*>(num_subgraphs_klasses);
    for (int i = 0; i < num_subgraphs_klasses; i++) {
      Klass* subgraph_k = subgraph_object_klasses->at(i);
      if (log_is_enabled(Info, cds, heap)) {
        ResourceMark rm;
        log_info(cds, heap)(
          "Archived object klass %s (%2d) => %s",
          _k->external_name(), i, subgraph_k->external_name());
      }
      _subgraph_object_klasses->at_put(i, subgraph_k);
    }
  }
}

struct CopyKlassSubGraphInfoToArchive : StackObj {
  CompactHashtableWriter* _writer;
  CopyKlassSubGraphInfoToArchive(CompactHashtableWriter* writer) : _writer(writer) {}

  bool do_entry(Klass* klass, KlassSubGraphInfo& info) {
    if (info.subgraph_object_klasses() != NULL || info.subgraph_entry_fields() != NULL) {
      ArchivedKlassSubGraphInfoRecord* record =
        (ArchivedKlassSubGraphInfoRecord*)MetaspaceShared::read_only_space_alloc(sizeof(ArchivedKlassSubGraphInfoRecord));
      record->init(&info);

      unsigned int hash = primitive_hash<Klass*>(klass);
      uintx deltax = MetaspaceShared::object_delta(record);
      guarantee(deltax <= MAX_SHARED_DELTA, "must not be");
      u4 delta = u4(deltax);
      _writer->add(hash, delta);
    }
    return true; // keep on iterating
  }
};

// Build the records of archived subgraph infos, which include:
// - Entry points to all subgraphs from the containing class mirror. The entry
//   points are static fields in the mirror. For each entry point, the field
//   offset, value and is_closed_archive flag are recorded in the sub-graph
//   info. The value is stored back to the corresponding field at runtime.
// - A list of klasses that need to be loaded/initialized before archived
//   java object sub-graph can be accessed at runtime.
void HeapShared::write_subgraph_info_table() {
  // Allocate the contents of the hashtable(s) inside the RO region of the CDS archive.
  DumpTimeKlassSubGraphInfoTable* d_table = _dump_time_subgraph_info_table;
  CompactHashtableStats stats;

  _run_time_subgraph_info_table.reset();

  int num_buckets = CompactHashtableWriter::default_num_buckets(d_table->_count);
  CompactHashtableWriter writer(num_buckets, &stats);
  CopyKlassSubGraphInfoToArchive copy(&writer);
  _dump_time_subgraph_info_table->iterate(&copy);

  writer.dump(&_run_time_subgraph_info_table, "subgraphs");
}

void HeapShared::serialize_subgraph_info_table_header(SerializeClosure* soc) {
  _run_time_subgraph_info_table.serialize_header(soc);
}

bool HeapShared::initialize_from_archived_subgraph(Klass* k) {
  if (!open_archive_heap_region_mapped()) {
    return false; // nothing to do
  }
  assert(!DumpSharedSpaces, "Should not be called with DumpSharedSpaces");

  Thread* THREAD = Thread::current();
  ResourceMark rm(THREAD);
  InstanceKlass* ik = InstanceKlass::cast(k);
  if (ik->is_pre_initialized_without_dependency_class()) {
    // Only has primitive type statics. Fully pre-initialized.
    log_info(preinit)("%s static initializer has no dependency class, "
                      "is fully pre-initialized", k->external_name());
    return true;
  }

  unsigned int hash = primitive_hash<Klass*>(k);
  ArchivedKlassSubGraphInfoRecord* record = _run_time_subgraph_info_table.lookup(k, hash, 0);
  if (record == NULL) {
    if (k->is_pre_initialized_with_dependency_class()) {
      log_info(preinit)("%s is pre-initialized, dependencies are super types",
                        k->external_name());
      return true;
    } else {
      log_info(preinit)("%s is not pre-initialized", k->external_name());
      return false;
    }
  }

  log_info(preinit)("initializing %s from archived subgraph",
                    k->external_name());

  // Initialize from archived data.
  Handle loader (THREAD, k->class_loader());
  Handle protection_domain (THREAD, k->protection_domain());

  int i;
  // Load/link/initialize the klasses of the objects in the subgraph.
  // The current klass k's loader is used.
  Array<Klass*>* klasses = record->subgraph_object_klasses();
  if (klasses != NULL) {
    for (i = 0; i < klasses->length(); i++) {
      Klass* obj_k = klasses->at(i);
      Klass* resolved_k = SystemDictionary::resolve_or_null(
                   (obj_k)->name(), loader, protection_domain, THREAD);
      if (resolved_k != obj_k) {
        log_info(cds, heap)("Failed to load subgraph because %s was not "
                            "loaded from archive", resolved_k->external_name());
        return false;
      }
      if ((obj_k)->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(obj_k);
        ik->initialize(THREAD);
      } else if ((obj_k)->is_objArray_klass()) {
        ObjArrayKlass* oak = ObjArrayKlass::cast(obj_k);
        oak->initialize(THREAD);
      }
    }
  }

  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
    // None of the field value(s) will be set if there was an exception.
    // The java code will not see any of the archived objects in the
    // subgraphs referenced from k in this case.
    log_info(preinit)(
      "Exception happened during initializing %s dependency classes",
      k->external_name());
    return false;
  }

  if (!record->is_partial_pre_init()) {
    // Fully pre-initialized.
    assert(k->is_pre_initialized_with_dependency_class(), "sanity");
    log_info(preinit)("%s is fully pre-initialized", k->external_name());
    return true;
  }

  // Load the subgraph entry fields from the record and store them back to
  // the corresponding fields within the mirror. Protected by the current
  // klass' init_lock.
  // No need to materialize the objects and write back to the fields
  oop m = k->java_mirror();
  Array<juint>* entry_field_records = record->entry_field_records();
  if (entry_field_records != NULL) {
    HandleMark hm(THREAD);
    Handle h_init_lock(THREAD, ik->init_lock());
    ObjectLocker ol(h_init_lock, THREAD, h_init_lock() != NULL);

    int efr_len = entry_field_records->length();
    assert(efr_len % 3 == 0, "sanity");
    for (i = 0; i < efr_len;) {
      int field_offset = entry_field_records->at(i);
      narrowOop nv = entry_field_records->at(i+1);
      int is_closed_archive = entry_field_records->at(i+2);
      oop v;
      if (is_closed_archive == 0) {
        // It's an archived object in the open archive heap regions, not shared.
        // The object refereced by the field becomes 'known' by GC from this
        // point. All objects in the subgraph reachable from the object are
        // also 'known' by GC.
        v = materialize_archived_object(nv);
      } else {
        // Shared object in the closed archive heap regions. Decode directly.
        assert(!CompressedOops::is_null(nv), "shared object is null");
        v = HeapShared::decode_from_archive(nv);
      }
      m->obj_field_put(field_offset, v);
      i += 3;

      log_debug(cds, heap)("  " PTR_FORMAT " init field @ %2d = " PTR_FORMAT,
                           p2i(k), field_offset, p2i(v));
    }

    // Done. Java code can see the archived sub-graphs referenced from k's
    // mirror after this point.
    log_info(preinit)("%s " PTR_FORMAT " is partially pre-initialized",
                        k->external_name(), p2i(k));
  }
  return false;
}

class WalkOopAndArchiveClosure: public BasicOopIterateClosure {
  int _level;
  bool _is_closed_archive;
  bool _record_klasses_only;
  // If _check_preservable_only is true, the current walk only checks subgraphs
  // without archiving.
  bool _check_preservable_only;
  KlassSubGraphInfo* _subgraph_info;
  oop _orig_referencing_obj;
  oop _archived_referencing_obj;
  bool _is_preservable;
  Thread* _thread;
 public:
  WalkOopAndArchiveClosure(int level,
                           bool is_closed_archive,
                           bool record_klasses_only,
                           bool check_preservable_only,
                           KlassSubGraphInfo* subgraph_info,
                           oop orig, oop archived, TRAPS) :
    _level(level), _is_closed_archive(is_closed_archive),
    _record_klasses_only(record_klasses_only),
    _check_preservable_only(check_preservable_only),
    _subgraph_info(subgraph_info),
    _orig_referencing_obj(orig), _archived_referencing_obj(archived),
    _is_preservable(true), _thread(THREAD) {}
  void do_oop(narrowOop *p) { WalkOopAndArchiveClosure::do_oop_work(p); }
  void do_oop(      oop *p) { WalkOopAndArchiveClosure::do_oop_work(p); }

 protected:
  template <class T> void do_oop_work(T *p) {
    oop obj = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(obj)) {
      assert(!HeapShared::is_archived_object(obj),
             "original objects must not point to archived objects");

      size_t field_delta = pointer_delta(p, _orig_referencing_obj, sizeof(char));
      T* new_p = (T*)(address(_archived_referencing_obj) + field_delta);
      Thread* THREAD = _thread;

      if (!_record_klasses_only && log_is_enabled(Debug, cds, heap)) {
        ResourceMark rm;
        log_debug(cds, heap)("(%d) %s[" SIZE_FORMAT "] ==> " PTR_FORMAT " size %d %s", _level,
                             _orig_referencing_obj->klass()->external_name(), field_delta,
                             p2i(obj), obj->size() * HeapWordSize, obj->klass()->external_name());
        LogTarget(Trace, cds, heap) log;
        LogStream out(log);
        obj->print_on(&out);
      }

      if (_check_preservable_only) {
        // Only walk the rest of the subgraph if we haven't encountered any
        // non-preservable objects.
        if (_is_preservable) {
          _is_preservable = HeapShared::check_reachable_objects_from(
                              _level + 1, obj, THREAD);
        }
      } else {
        // Recursively walk and archive all reachable objects from the
        // current one.
        oop archived = HeapShared::archive_reachable_objects_from(
            _level + 1, _subgraph_info, obj, _is_closed_archive, THREAD);
        assert(archived != NULL,
               "VM should have exited with unarchivable objects for _level > 1");
        assert(HeapShared::is_archived_object(archived), "must be");

        if (!_record_klasses_only) {
          // Update the reference in the archived copy of the referencing object.
          log_debug(cds, heap)("(%d) updating oop @[" PTR_FORMAT "] " PTR_FORMAT " ==> " PTR_FORMAT,
                               _level, p2i(new_p), p2i(obj), p2i(archived));
          RawAccess<IS_NOT_NULL>::oop_store(new_p, archived);
        }
      }
    }
  }

 public:
  bool is_preservable() {
    return _is_preservable;
  }
};

void HeapShared::check_closed_archive_heap_region_object(InstanceKlass* k,
                                                         Thread* THREAD) {
  // Check fields in the object
  for (JavaFieldStream fs(k); !fs.done(); fs.next()) {
    if (!fs.access_flags().is_static()) {
      BasicType ft = fs.field_descriptor().field_type();
      if (!fs.access_flags().is_final() && (ft == T_ARRAY || T_OBJECT)) {
        ResourceMark rm(THREAD);
        log_warning(cds, heap)(
          "Please check reference field in %s instance in closed archive heap region: %s %s",
          k->external_name(), (fs.name())->as_C_string(),
          (fs.signature())->as_C_string());
      }
    }
  }
}

// (1) If orig_obj has not been archived yet, archive it.
// (2) If orig_obj has not been seen yet (since start_recording_subgraph() was called),
//     trace all  objects that are reachable from it, and make sure these objects are archived.
// (3) Record the klasses of all orig_obj and all reachable objects.
oop HeapShared::archive_reachable_objects_from(int level,
                                               KlassSubGraphInfo* subgraph_info,
                                               oop orig_obj,
                                               bool is_closed_archive,
                                               TRAPS) {
  assert(orig_obj != NULL, "must be");
  assert(!is_archived_object(orig_obj), "sanity");

  bool is_mirror = false;

  // A java.lang.Class instance can be included in an archived object
  // sub-graph, if the instance is the same object as the klass mirror.
  // Don't walk the references from the mirror object. The Klass of the
  // mirror object is added to the klass dependency list.
  if (java_lang_Class::is_instance(orig_obj)) {
    // During the walk for checking the subgraphs, check_reachable_objects_from
    // makes sure only archived mirror objects are allowed for j.l.Class
    // instances.
    assert((java_lang_Class::as_Klass(orig_obj) != NULL &&
            java_lang_Class::as_Klass(orig_obj)->java_mirror() == orig_obj) ||
           java_lang_Class::is_primitive(orig_obj),
           "must be mirror");
    is_mirror = true;
  }

  oop archived_obj = find_archived_heap_object(orig_obj);
  if (java_lang_String::is_instance(orig_obj) && archived_obj != NULL) {
    // To save time, don't walk strings that are already archived. They just contain
    // pointers to a type array, whose klass doesn't need to be recorded.
    return archived_obj;
  }

  if (has_been_seen_during_subgraph_recording(orig_obj)) {
    // orig_obj has already been archived and traced. Nothing more to do.
    return archived_obj;
  } else {
    set_has_been_seen_during_subgraph_recording(orig_obj);
  }

  bool record_klasses_only = (archived_obj != NULL);
  if (archived_obj == NULL) {
    assert(!is_mirror, "Mirror object must be archived already");
    ++_num_new_archived_objs;
    archived_obj = archive_heap_object(orig_obj, THREAD);
    if (archived_obj == NULL) {
      // Skip archiving the sub-graph referenced from the current entry field.
      ResourceMark rm;
      log_error(cds, heap)(
        "Cannot archive the sub-graph referenced from %s object ("
        PTR_FORMAT ") size %d, skipped.",
        orig_obj->klass()->external_name(), p2i(orig_obj), orig_obj->size() * HeapWordSize);
      if (level == 1) {
        // Don't archive a subgraph root that's too big. For archives static fields, that's OK
        // as the Java code will take care of initializing this field dynamically.
        return NULL;
      } else {
        // We don't know how to handle an object that has been archived, but some of its reachable
        // objects cannot be archived. Bail out for now. We might need to fix this in the future if
        // we have a real use case.
        vm_exit(1);
      }
    }
  }

  // Add the archived object's klass type to the subgraph dependency klass list.
  assert(archived_obj != NULL, "must be");
  Klass *orig_k = orig_obj->klass();
  Klass *relocated_k = archived_obj->klass();
  subgraph_info->add_subgraph_object_klass(orig_k, relocated_k);

  if (!is_mirror) {
    // Walk all references in the object and archive.
    WalkOopAndArchiveClosure walker(level, is_closed_archive, record_klasses_only,
                                    false /* check_preservable_only false */,
                                    subgraph_info, orig_obj, archived_obj, THREAD);
    orig_obj->oop_iterate(&walker);
    if (is_closed_archive && orig_k->is_instance_klass()) {
      check_closed_archive_heap_region_object(InstanceKlass::cast(orig_k), THREAD);
    }
  } else {
    // This is an archived mirror object. No need to walk the mirror:
    // - All non-static fields in archived mirrors are cleared;
    // - Any of the non-preservable static fields in archived mirrors are reset
    //   to the default values by java_lang_Class::process_archived_mirror;
    // - All preservable static fields in both partially pre-initialized (only
    //   some static fields are initialized and preserved) and fully
    //   pre-initialized class mirrors are handled explicitly. See
    //   archive_preservable_static_field_subgraphs() and
    //   archive_preservable_klass_static_fields_subgraphs();
    // - Preserved static fields in archived mirrors are handled by
    //   initialize_from_archived_subgraph() at runtime during the corresponding
    //   class' initialization time.
    Klass *orig_as_k = java_lang_Class::as_Klass(orig_obj);
    log_debug(cds, heap)("Archived %s mirror object" PTR_FORMAT " => " PTR_FORMAT,
        orig_as_k == NULL ? "primitive type" : orig_as_k->external_name(),
        p2i(orig_obj), p2i(archived_obj));
  }
  return archived_obj;
}

//
// Start from the given static field in a java mirror and archive the
// complete sub-graph of java heap objects that are reached directly
// or indirectly from the starting object by following references.
// Sub-graph archiving restrictions (current):
//
// - All classes of objects in the archived sub-graph (including the
//   entry class) must be class loaded by the builtin class loaders.
// - No non-mirror java.lang.Class instance can be included inside
//   an archived sub-graph. Mirrors can be sub-graphs' entry objects,
//   and can be included in sub-graphs.
//
// The Java heap object sub-graph archiving process (see
// WalkOopAndArchiveClosure):
//
// 1) Java object sub-graph archiving starts from a given static field
// within a Class instance (java mirror). If the static field is a
// refererence field and points to a non-null java object, proceed to
// the next step.
//
// 2) Archives the referenced java object. If an archived copy of the
// current object already exists, updates the pointer in the archived
// copy of the referencing object to point to the current archived object.
// Otherwise, proceed to the next step.
//
// 3) Follows all references within the current java object and recursively
// archive the sub-graph of objects starting from each reference.
//
// 4) Updates the pointer in the archived copy of referencing object to
// point to the current archived object.
//
// 5) The Klass of the current java object is added to the list of Klasses
// for loading and initialzing before any object in the archived graph can
// be accessed at runtime.
//
// For classes that are not annotated with @Preserve but with @Preserve
// annotated static fields, 'is_partial_pre_init' is true. References to
// those archived field values are stored separately in KlassSubGraphInfo
// records and are not preserved with the corresponding mirror objects.
//
oop HeapShared::archive_reachable_objects_from_static_field(InstanceKlass *k,
                                                            const char* klass_name,
                                                            int field_offset,
                                                            const char* field_name,
                                                            bool is_closed_archive,
                                                            bool is_partial_pre_init,
                                                            TRAPS) {
  assert(DumpSharedSpaces, "dump time only");
  assert(k->shared_classpath_index() >= 0, "must be BUILTIN type");

  oop m = k->java_mirror();

  KlassSubGraphInfo* subgraph_info = get_subgraph_info(k, is_partial_pre_init);
  oop f = m->obj_field(field_offset);

  log_debug(cds, heap)("Start archiving from: %s::%s (" PTR_FORMAT ")", klass_name, field_name, p2i(f));

  if (!CompressedOops::is_null(f)) {
    if (log_is_enabled(Trace, cds, heap)) {
      LogTarget(Trace, cds, heap) log;
      LogStream out(log);
      f->print_on(&out);
    }

    oop af = archive_reachable_objects_from(1, subgraph_info, f,
                                            is_closed_archive, CHECK_NULL);

    if (af == NULL) {
      log_error(cds, heap)("Archiving failed %s::%s (some reachable objects cannot be archived)",
                           klass_name, field_name);
    } else {
      if (is_partial_pre_init) {
        // Note: the field value is not preserved in the archived mirror.
        // Record the field as a new subGraph entry point. The recorded
        // information is restored from the archive at runtime.
        subgraph_info->add_subgraph_entry_field(field_offset, af,
                                                is_closed_archive);
        log_info(cds, heap, subgraphinfo)(
          "Recorded subgraph entry field (class partial pre-init) %s::%s",
          klass_name, field_name);
      }
      log_info(cds, heap)("Archived field %s::%s => " PTR_FORMAT,
                          klass_name, field_name, p2i(af));
      return af;
    }
  } else {
    // The field contains null, we still need to record the entry point,
    // so it can be restored at runtime.
    if (is_partial_pre_init) {
      subgraph_info->add_subgraph_entry_field(field_offset, NULL, false);
      log_info(cds, heap, subgraphinfo)(
        "Recorded subgraph entry field (class partial pre-init) %s::%s, "
        "value is NULL", klass_name, field_name);
    }
  }
  return NULL;
}

#ifndef PRODUCT
class VerifySharedOopClosure: public BasicOopIterateClosure {
 private:
  bool _is_archived;

 public:
  VerifySharedOopClosure(bool is_archived) : _is_archived(is_archived) {}

  void do_oop(narrowOop *p) { VerifySharedOopClosure::do_oop_work(p); }
  void do_oop(      oop *p) { VerifySharedOopClosure::do_oop_work(p); }

 protected:
  template <class T> void do_oop_work(T *p) {
    oop obj = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(obj)) {
      HeapShared::verify_reachable_objects_from(obj, _is_archived);
    }
  }
};

void HeapShared::verify_subgraph_from_static_field(InstanceKlass* k, int field_offset) {
  assert(DumpSharedSpaces, "dump time only");
  assert(k->shared_classpath_index() >= 0, "must be BUILTIN type");

  oop m = k->java_mirror();
  oop f = m->obj_field(field_offset);
  if (!CompressedOops::is_null(f)) {
    verify_subgraph_from(f);
  }
}

void HeapShared::verify_subgraph_from(oop orig_obj) {
  oop archived_obj = find_archived_heap_object(orig_obj);
  if (archived_obj == NULL) {
    // It's OK for the root of a subgraph to be not archived. See comments in
    // archive_reachable_objects_from().
    return;
  }

  // Verify that all objects reachable from orig_obj are archived.
  init_seen_objects_table();
  verify_reachable_objects_from(orig_obj, false);
  delete_seen_objects_table();

  // Note: we could also verify that all objects reachable from the archived
  // copy of orig_obj can only point to archived objects, with:
  //      init_seen_objects_table();
  //      verify_reachable_objects_from(archived_obj, true);
  //      init_seen_objects_table();
  // but that's already done in G1HeapVerifier::verify_archive_regions so we
  // won't do it here.
}

void HeapShared::verify_reachable_objects_from(oop obj, bool is_archived) {
  _num_total_verifications ++;
  if (!has_been_seen_during_subgraph_recording(obj)) {
    set_has_been_seen_during_subgraph_recording(obj);

    if (is_archived) {
      assert(is_archived_object(obj), "must be");
      assert(find_archived_heap_object(obj) == NULL, "must be");
    } else {
      assert(!is_archived_object(obj), "must be");
      assert(find_archived_heap_object(obj) != NULL, "must be");
    }

    VerifySharedOopClosure walker(is_archived);
    obj->oop_iterate(&walker);
  }
}
#endif

HeapShared::ObjectsTable* HeapShared::_seen_objects_table = NULL;
int HeapShared::_num_new_walked_objs;
int HeapShared::_num_new_archived_objs;
int HeapShared::_num_old_recorded_klasses;

int HeapShared::_num_total_subgraph_recordings = 0;
int HeapShared::_num_total_walked_objs = 0;
int HeapShared::_num_total_archived_objs = 0;
int HeapShared::_num_total_recorded_klasses = 0;
int HeapShared::_num_total_verifications = 0;

bool HeapShared::has_been_seen_during_subgraph_recording(oop obj) {
  return _seen_objects_table->get(obj) != NULL;
}

void HeapShared::set_has_been_seen_during_subgraph_recording(oop obj) {
  assert(!has_been_seen_during_subgraph_recording(obj), "sanity");
  _seen_objects_table->put(obj, true);
  ++ _num_new_walked_objs;
}

void HeapShared::start_recording_subgraph(InstanceKlass *k, const char* class_name) {
  log_info(cds, heap)("Start recording subgraph(s) for archived fields in %s", class_name);
  init_seen_objects_table();
  _num_new_walked_objs = 0;
  _num_new_archived_objs = 0;
  Klass* relocated_k = MetaspaceShared::get_relocated_klass(k);
  KlassSubGraphInfo* ksg = find_subgraph_info(relocated_k);
  _num_old_recorded_klasses =
    (ksg == NULL) ? 0 : ksg->num_subgraph_object_klasses();
}

void HeapShared::done_recording_subgraph(InstanceKlass *k, const char* class_name) {
  Klass* relocated_k = MetaspaceShared::get_relocated_klass(k);
  KlassSubGraphInfo* info = find_subgraph_info(relocated_k);

  int num_new_recorded_klasses = (info == NULL) ? 0 :
    info->num_subgraph_object_klasses() - _num_old_recorded_klasses;
  log_info(cds, heap)("Done recording subgraph(s) for archived fields in %s: "
                      "walked %d objs, archived %d new objs, recorded %d classes",
                      class_name, _num_new_walked_objs, _num_new_archived_objs,
                      num_new_recorded_klasses);

  delete_seen_objects_table();

  if (info != NULL) {
    _num_total_subgraph_recordings ++;
    _num_total_walked_objs      += _num_new_walked_objs;
    _num_total_archived_objs    += _num_new_archived_objs;
    _num_total_recorded_klasses +=  num_new_recorded_klasses;
  }
}

class ArchivableStaticFieldFinder: public FieldClosure {
  InstanceKlass* _ik;
  Symbol* _field_name;
  bool _found;
  int _offset;
public:
  ArchivableStaticFieldFinder(InstanceKlass* ik, Symbol* field_name) :
    _ik(ik), _field_name(field_name), _found(false), _offset(-1) {}

  virtual void do_field(fieldDescriptor* fd) {
    if (fd->name() == _field_name) {
      assert(!_found, "fields cannot be overloaded");
      assert(fd->field_type() == T_OBJECT || fd->field_type() == T_ARRAY, "can archive only obj or array fields");
      _found = true;
      _offset = fd->offset();
    }
  }
  bool found()     { return _found;  }
  int offset()     { return _offset; }
};

void HeapShared::initialize_preservable_static_field_infos(Thread* THREAD) {
  if (_preservable_static_fields == NULL) {
    return;
  }

  assert(PreInitializeArchivedClass,
         "should has no preservable static fields when PreInitializeArchivedClass is false");

  for (int i = 0; i < _preservable_static_fields->length(); i++) {
    PreservableStaticFieldInfo* info = _preservable_static_fields->at(i);

    Klass* k = SystemDictionary::resolve_or_null(info->klass_name(), THREAD);
    assert(k != NULL && !HAS_PENDING_EXCEPTION, "class must exist");
    InstanceKlass* ik = InstanceKlass::cast(k);
    assert(InstanceKlass::cast(ik)->is_shared_boot_class(),
           "Only support boot classes");
    ik->initialize(THREAD);
    guarantee(!HAS_PENDING_EXCEPTION, "exception in initialize");

    ArchivableStaticFieldFinder finder(ik, info->field_name());
    ik->do_local_static_fields(&finder);
    assert(finder.found(), "field must exist");

    info->set_klass(ik);
    info->set_offset(finder.offset());
  }
}

void HeapShared::initialize_subgraph_entry_fields(Thread* THREAD) {
  _dump_time_subgraph_info_table =
    new (ResourceObj::C_HEAP, mtClass)DumpTimeKlassSubGraphInfoTable();

  // Initialize classes with any static fields annotated with @Preserve.
  initialize_preservable_static_field_infos(THREAD);
}

void HeapShared::initialize_preservable_klass_from_list(Thread* THREAD) {
  // Initialize preservable classes from given class list, the content format
  // of class list looks as well as DumpLoadedClassList
  if (!PreInitializeArchivedClassList) {
    return;
  }
  if (!preinit_classlist_file->is_open()) {
    log_error(preinit)("Can not open extended preservable class list file %s", PreInitializeArchivedClassList);
    return;
  }
  while (!preinit_classlist_file->eof()) {
    // Max number of bytes allowed per line in the classlist.
    // Theoretically Java class names could be 65535 bytes in length. Also, an input line
    // could have a very long path name up to JVM_MAXPATHLEN bytes in length. In reality,
    // 4K bytes is more than enough.
    ResourceMark rm;
    char buf[4096];
    char* klass_line = preinit_classlist_file->readln(buf, 4096);
    if (klass_line == NULL) {
      continue;
    }
    if (strlen(buf) > 0) {
      Symbol* klass_name = SymbolTable::new_symbol(klass_line, THREAD);
      Klass* k = SystemDictionary::resolve_or_null(klass_name,
                                   Handle(THREAD, SystemDictionary::java_system_loader()),
                                   Handle()/*null_protection_domain*/, THREAD);
      if (k == NULL) {
        if (HAS_PENDING_EXCEPTION) {
#ifndef PRODUCT
          if (Verbose) {
            Handle throwable(THREAD, PENDING_EXCEPTION);
            java_lang_Throwable::print_stack_trace(throwable, tty);
            tty->cr();
          }
#endif
          CLEAR_PENDING_EXCEPTION;
        }
        log_warning(preinit)("Failed to load klass %s", klass_name->as_C_string());
        continue;
      }
      if (k->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(k);
        HeapShared::set_can_preserve(ik, false);
        HeapShared::add_preservable_class(ik);
      }
    }
  }
}

// Archive all individual static fields that are annotated with @Preserve.
// The containing classes are not annotated with @Preserve and the remaining
// static fields within those classes are not archived. As a result, the
// archived containing classes are partially pre-initialized.
void HeapShared::archive_preservable_static_field_subgraphs(Thread* THREAD) {
  _num_total_subgraph_recordings = 0;
  _num_total_walked_objs = 0;
  _num_total_archived_objs = 0;
  _num_total_recorded_klasses = 0;
  _num_total_verifications = 0;

  // For each class X that has one or more archived fields:
  // [1] Dump the subgraph of each archived field
  // [2] Create a list of all the class of the objects that can be reached
  //     by any of these static fields.
  //     At runtime, these classes are initialized before X's archived fields
  //     are restored by HeapShared::initialize_from_archived_subgraph().
  int i;
  int len = _preservable_static_fields->length();
  for (i = 0; i < len;) {
    PreservableStaticFieldInfo* info = _preservable_static_fields->at(i);
    // Skip any fields that are found not preservable during the subgraph
    // checking phase.
    if (info->can_preserve()) {
      assert(info->klass() != NULL, "sanity");
      Symbol* klass_name = info->klass_name();
      const char* klass_name_str = klass_name->as_C_string();
      start_recording_subgraph(info->klass(), klass_name_str);

      // Archive all static fields from the same class together.
      for (; i < len; i++) {
        PreservableStaticFieldInfo* f = _preservable_static_fields->at(i);
        if (f->klass_name() != klass_name) {
          break;
        }
        if (info->can_preserve()) {
          archive_reachable_objects_from_static_field(f->klass(),
                                                      klass_name_str,
                                                      f->offset(),
                                                      f->field_name()->as_C_string(),
                                                      false,
                                                      true, CHECK);
        }
      }
      done_recording_subgraph(info->klass(), klass_name_str);
    } else {
      i ++; // Skip the one that cannot be preserved.
    }
  }

  log_info(cds, heap)("Archived subgraph records in open archive heap region = %d",
                      _num_total_subgraph_recordings);
  log_info(cds, heap)("  Walked %d objects", _num_total_walked_objs);
  log_info(cds, heap)("  Archived %d objects", _num_total_archived_objs);
  log_info(cds, heap)("  Recorded %d klasses", _num_total_recorded_klasses);

#ifndef PRODUCT
  for (int i = 0; i < _preservable_static_fields->length(); i++) {
    PreservableStaticFieldInfo* f = _preservable_static_fields->at(i);
    if (f->can_preserve() && f->klass() != NULL) {
      verify_subgraph_from_static_field(f->klass(), f->offset());
    }
  }
  log_info(cds, heap)("  Verified %d references", _num_total_verifications);
#endif
}

//////////////////////////////////////////////////////////////////////////
//
// Support for pre-initializing and archiving classes with @Preserve annotation.
//
// A class annotated with @Preserve is initialized at dump time. All static
// fields within the class are preserved in the archive. For each archived
// static field, the complete graph of reachable objects is copied into the
// archive heap region and archived.
//
// At runtime, when the class is initialized, all preserved values
// are retrieved from the archive and installed into the static fields. The
// execution of <clinit> is skipped if the archived static fields are
// successfully installed.
//
// Following is the overview of the related dump time procedure:
//
// 1) Initialize classes annotated with @Preserve.
// 2) Remove unshareable info in archivable classes (also reset the related
//    InstanceKlass _init_states to 'allocated'.
// 3) Archive/relocate meta-objects.
// 4) Archive mirror objects. All non-static fields in archived mirrors are
//    cleared. All local static fields in classes without @Preserve are reset
//    to the default values.
// 5) Check if subgraphs reachable from static fields are preservable. The
//    local static fields in a class with @Preserve is reset to the default
//    values if PreservableKlassChecker finds any static fields cannot be
//    preserved.
// 6) Archive all preservable subgraphs.
//
///////////////////////////////////////////////////////////////////////////

// Initial size for _preservable_static_fields list at Google scale
const static int INITIAL_LIST_SIZE = 200000;

bool HeapShared::_can_add_preserve_klasses = true;
HeapShared::ObjectsTable* HeapShared::_not_preservable_object_cache = NULL;
GrowableArray<PreservableStaticFieldInfo*>* HeapShared::_preservable_static_fields = NULL;
// A list of preservable classes.
HeapShared::PreInitializedPreservableKlasses* HeapShared::_preservable_klasses = NULL;

// Closure for checking if a subgraph referenced from a reference type
// static field is preservable. Please see more details in comments above
// HeapShared::check_reachable_objects_from().
class StaticFieldChecker: public FieldClosure {
 private:
  InstanceKlass* _ik;
  bool _strict;
  oop  _mirror;
  bool _all_fields_preservable;
  Thread* _thread;

 public:
  StaticFieldChecker(InstanceKlass* ik, bool strict, Thread* thread) :
    _ik(ik), _strict(strict), _all_fields_preservable(true),
    _thread(thread) {
    _mirror = ik->java_mirror();
  }

  void do_field(fieldDescriptor* fd) {
    assert(DumpSharedSpaces, "dump time only");
    if (!_all_fields_preservable) {
      return;
    }

    BasicType ft = fd->field_type();
    switch (ft) {
      case T_ARRAY:
      case T_OBJECT: {
        ResourceMark rm;
        log_trace(cds, heap)(
                  "Checking static field %s.%s(%s)",
                  _ik->external_name(),
                  fd->name()->as_C_string(),
                  fd->signature()->as_C_string());

        oop o = _mirror->obj_field(fd->offset());
        if (!CompressedOops::is_null(o)) {
          if (_strict) {
            if (!(fd->is_final()) || !(fd->access_flags().is_stable())) {
              if (!java_lang_String::is_instance(o)) {
                ResourceMark rm;
                _all_fields_preservable = false;
                log_trace(cds, heap)(
                  "Static field %s.%s(%s) is not final or stable. "
                  "Class cannot be preserved.",
                  _ik->external_name(),
                  fd->name()->as_C_string(),
                  fd->signature()->as_C_string());
                break;
              }
            }
          }
          _all_fields_preservable =
            HeapShared::check_reachable_objects_from(1, o, _thread);
        }
        break;
      }
      default:
        break;
     }
  }

  bool all_fields_preservable() {
    return _all_fields_preservable;
  }
};

class StaticFieldArchiver: public FieldClosure {
  InstanceKlass* _ik;
  oop _archived_mirror;
  Thread* _thread;
public:
  StaticFieldArchiver(InstanceKlass* ik, oop archived_mirror,
                      Thread* thread) :
    _ik(ik), _archived_mirror(archived_mirror), _thread(thread) {}

  virtual void do_field(fieldDescriptor* fd) {
    BasicType ft = fd->field_type();
    if (ft == T_ARRAY || ft == T_OBJECT) {
      int field_offset = fd->offset();
      oop archived_v = HeapShared::archive_reachable_objects_from_static_field(
                         _ik, _ik->external_name(), field_offset,
                         fd->name()->as_klass_external_name(),
                         false, false, _thread);
      _archived_mirror->obj_field_put_raw(field_offset, archived_v);
    }
  }
};

void HeapShared::set_can_preserve(InstanceKlass *ik, bool is_annotated) {
  if (DumpSharedSpaces && PreInitializeArchivedClass &&
      _can_add_preserve_klasses) {
    if (ik->can_preserve()) {
      return;
    }
    ik->set_can_preserve();
    ResourceMark rm;
    log_info(preinit)("Set can_preserve for class %s(" PTR_FORMAT "), %s",
                      ik->external_name(), p2i(ik),
                      is_annotated ? "with @Preserve annotation" :
                                     "no <clinit> or static field");
  }
}

void HeapShared::add_preservable_class(InstanceKlass *ik) {
  if (!DumpSharedSpaces || !_can_add_preserve_klasses) {
    return;
  }

  assert(!ik->is_anonymous(), "Anonymous klass cannot be preserved");

  if (_preservable_klasses == NULL) {
    _preservable_klasses =
      new (ResourceObj::C_HEAP, mtClass)PreInitializedPreservableKlasses();
  }
  _preservable_klasses->put(ik, true);
  ResourceMark rm;
  log_info(preinit)("Add preservable class %s(" PTR_FORMAT ")",
                    ik->external_name(), p2i(ik));
}

// Called by ClassFileParser when a static field with @Preserve is processed.
void HeapShared::add_preservable_static_field(Symbol* class_name,
                                              Symbol* field_name) {
  if (!DumpSharedSpaces || !PreInitializeArchivedClass) {
    return;
  }

  if (_preservable_static_fields == NULL) {
    _preservable_static_fields =
      new(ResourceObj::C_HEAP, mtClass)GrowableArray<PreservableStaticFieldInfo*>(
          INITIAL_LIST_SIZE, true);
  }

  PreservableStaticFieldInfo* field_info =
    new PreservableStaticFieldInfo(class_name,field_name);
  _preservable_static_fields->append(field_info);

  if (log_is_enabled(Debug, cds, heap)) {
    log_debug(cds, heap)("Found @Preserve annotated field %s.%s",
                         class_name->as_C_string(), field_name->as_C_string());
  }
}

// This is called when archiving mirrors after metadata relocation. The
// Klasses are from MetaspaceShared::collected_klasses(), which are already
// relocated at this point.
bool HeapShared::reset_klass_statics(Klass *k) {
  ResourceMark rm;
  if (k->is_instance_klass()) {
    InstanceKlass *ik = InstanceKlass::cast(k);
    // Support classes from BUILTIN class loaders.
    if (ik->shared_classpath_index() >= 0 &&
        ik->can_preserve()) {
      log_info(cds, heap)("Preserve static fields for %s", k->external_name());
      return false;
    }
  }
  log_info(cds, heap)("Reset static fields for %s", k->external_name());
  return true;
}

void HeapShared::initialize_preservable_klass(InstanceKlass *ik,
                                              Thread* THREAD) {
  if (!PreInitializeArchivedClass) {
    return;
  }

  if (ik->can_preserve()) {
    // Support all builtin class loaders
    if (ik->shared_classpath_index() >= 0) {
      ResourceMark rm(THREAD);
      log_info(cds, heap)("Initializing preservable class %s(" PTR_FORMAT ")",
                          ik->external_name(), p2i(ik));
      ik->initialize(THREAD);
      if (THREAD->has_pending_exception()) {
        THREAD->clear_pending_exception();
        ik->clear_can_preserve();
        return;
      }
      assert(ik->is_initialized(), "must be initialized");
    } else {
      ik->clear_can_preserve();
    }
  }
}

class PreservableKlassChecker {
  Thread* _thread;
 public:
  PreservableKlassChecker(Thread* thread) :
    _thread(thread) {}

  bool do_entry(Klass *k, bool v) {
    if (!k->can_preserve()) {
      return true;
    }

    if (k->is_instance_klass()) {
      InstanceKlass* ik = InstanceKlass::cast(k);

      // Support classes for builtin loaders.
      if (ik->shared_classpath_index() >= 0) {
        ResourceMark rm(_thread);
        log_debug(cds, heap)("Checking if class %s(" PTR_FORMAT ") is "
                             "preservable", ik->external_name(), p2i(ik));
        HeapShared::init_seen_objects_table();
        StaticFieldChecker checker(ik, false, _thread);
        ik->do_local_static_fields(&checker);
        if (checker.all_fields_preservable()) {
          log_info(cds, heap)("Class %s(" PTR_FORMAT ") is preservable",
                              ik->external_name(), p2i(ik));
        } else {
          ik->clear_can_preserve();
          // Reset all static fields in the archived mirror. The instance fields
          // in the mirror is already reset by
          // java_lang_Class::process_archived_mirror().
          oop m = ik->java_mirror();
          if (!java_lang_Class::is_primitive(m)) {
            oop archived_m = HeapShared::find_archived_heap_object(m);
            java_lang_Class::reset_mirror_static_fields(ik, archived_m, _thread);
          }
          log_info(cds, heap)("Class %s(" PTR_FORMAT ") is not preservable",
                              ik->external_name(), p2i(ik));
        }
        HeapShared::delete_seen_objects_table();
      }
    }
    return true;
  }
};

void HeapShared::check_preservable_klasses_and_fields(Thread* THREAD) {
  // The temporary cache is used during the subgraph object checking to
  // avoid walking any non-preservable objects more than once.
  _not_preservable_object_cache =
    new (ResourceObj::C_HEAP, mtClass)ObjectsTable();

  check_preservable_static_fields(THREAD);
  check_preservable_klasses(THREAD);

  delete _not_preservable_object_cache;
  _not_preservable_object_cache = NULL;
}

// Check if the static fields in classes annotated with @Preserve can be
// archived(preserved). See StaticFieldChecker for details. If any of the
// static fields in a class cannot be preserved, the _can_preserve flag is
// set to false in its Klass' _shared_class_flags.
void HeapShared::check_preservable_klasses(Thread* THREAD) {
  // Don't add any new class to the preservable classes at this point.
  _can_add_preserve_klasses = false;

  PreservableKlassChecker checker(THREAD);
  _preservable_klasses->iterate(&checker);
}

// Check individual static fields annotated with @Preserve. If a
// static field cannot be preserved, the corresponding
// PreservableStaticFieldInfo._can_preserve flag is set to false.
//
// Currently only support static fields in boot classes.
// TODO(b/168823639): Support for all builtin loaders.
void HeapShared::check_preservable_static_fields(Thread* THREAD) {
  if (_preservable_static_fields == NULL) {
    return;
  }

  for (int i = 0; i < _preservable_static_fields->length(); i++) {
    PreservableStaticFieldInfo* info = _preservable_static_fields->at(i);
    assert(info->can_preserve(), "can_preserve is already false");
    InstanceKlass* ik = info->klass();
    assert(ik != NULL, "class must exist");
    assert(ik->is_shared_boot_class(), "Only support boot classes");
    oop m = ik->java_mirror();
    oop o = m->obj_field(info->offset());
    HeapShared::init_seen_objects_table();
    if (!check_reachable_objects_from(1, o, THREAD)) {
      info->set_can_preserve(false);
    }
    HeapShared::delete_seen_objects_table();
  }
}

// Checks if an object within a subgraph can be preserved. An object should
// not be preserved if it is:
//
// - a Java object whose class type is anonymous class
// - a j.l.Class instance that is not a Klass mirror
// - a j.l.ProtectionDomain instance
// - an instance of j.l.ClassLoader or any of the subclasses
// - a j.l.Runnable instance
//
// A static field value should not be preserved in the archive if its subgraph
// contains any of the above objects.
bool HeapShared::check_reachable_objects_from(int level,
                                              oop obj,
                                              TRAPS) {
  assert(obj != NULL, "must be");

  bool is_preservable = true;
  bool walk_references = true;
  Klass *k = obj->klass();
  log_debug(cds, heap)("(%d) Checking if %s object (" PTR_FORMAT ") is "
                       "preservable", level, k->external_name(), p2i(obj));

  // It is safe to archive the object if it is locked as
  // HeapShared::archive_heap_object resets markword.
  if (!obj->is_unlocked()) {
    log_debug(cds, heap)(
              "(%d) Object(%s) is locked. Can be preserved.",
              level, k->external_name());
  }

  if (not_preservable_object_cache()->get(obj)) {
    is_preservable = false;
    log_debug(cds, heap)(
              "(%d) Object(%s) is already in not_preservable_object_cache.",
              level, k->external_name());
    return false;
  }

  if (k->is_instance_klass()) {
    InstanceKlass *ik = InstanceKlass::cast(k);
    if (ik->is_anonymous()) {
      is_preservable = false;
      log_debug(cds, heap)(
                "(%d) Object class is anonymous: %s. Cannot be preserved.",
                level, k->external_name());
    }

    if (java_lang_Class::is_instance(obj)) {
      // This is a java.lang.Class instance. A java.lang.Class instance can be
      // included in archived subgraphs if:
      //
      // It's the same object as the klass mirror, and the obj Klass type is
      // not an anonymous class.
      Klass* mirror_k = java_lang_Class::as_Klass(obj);
      if (mirror_k == NULL){
        // Check if it is a basic type mirror. Need to use the archived object,
        // as basic type mirrors in Universe::_mirrors[] are already relocated
        // at this point.
        if (Universe::is_basic_type_mirror(find_archived_heap_object(obj))) {
          walk_references = false;
          log_debug(cds, heap)(
            "(%d) java.lang.Class object (" PTR_FORMAT ") is primitive type "
            "mirror. Can be included in the archived sub-graph.",
            level, p2i(obj));
        } else {
          is_preservable = false;
          log_debug(cds, heap)(
            "(%d) java.lang.Class object (" PTR_FORMAT ") is not mirror."
            "Cannot be preserved.", level, p2i(obj));
        }
      } else if (mirror_k->is_instance_klass()) {
        // The object's Klass type is an InstanceKlass.
        if (!InstanceKlass::cast(mirror_k)->is_anonymous()) {
          if (obj == mirror_k->java_mirror() &&
              HeapShared::find_archived_heap_object(obj) != NULL) {
            // This is an archived mirror object. Don't follow
            // the references from mirror.
            walk_references = false;
            assert(obj == mirror_k->java_mirror(), "mirror object is different");
            log_debug(cds, heap)(
              "(%d) java.lang.Class object (" PTR_FORMAT ") (%s) is a mirror object. "
              "Can be included in the archived sub-graph.",
              level, p2i(obj), java_lang_Class::as_external_name(obj));
          } else {
            // The java.lang.Class instance is not a mirror and cannot be
            // included in an archived object sub-graph since it contains
            // references to ClassLoader object.
            is_preservable = false;
            log_debug(cds, heap)(
              "(%d) java.lang.Class object (%s) Klass is not archived mirror."
              "Cannot be preserved.",
              level, java_lang_Class::as_external_name(obj));
          }
        } else {
          // Anonymous klasses are not archived.
          is_preservable = false;
          log_debug(cds, heap)(
            "(%d) java.lang.Class object (%s) Klass is anonymous."
            "Cannot be preserved.",
            level, java_lang_Class::as_external_name(obj));
        }
      } else if (mirror_k->is_array_klass()) {
        // The array klass field at that the _array_klass_offset must not be
        // NULL.
        if (java_lang_Class::array_klass_acquire(obj) != NULL) {
          // This is a mirror object. Don't follow the references from mirror.
          walk_references = false;
          assert(obj == mirror_k->java_mirror(), "mirror object is different");
          log_debug(cds, heap)(
            "(%d) java.lang.Class object " PTR_FORMAT "(%s) is an array klass "
            "mirror object. Can be included in the archived sub-graph.",
            level, p2i(obj), java_lang_Class::as_external_name(obj));
        }
      }
    } else if (java_lang_ClassLoader::is_instance(obj)) {
      is_preservable = false;
      log_debug(cds, heap)(
        "(%d) java.lang.ClassLoader object is in the archived sub-graph. "
        "Cannot be preserved.", level);
    } else if (ik == SystemDictionary::ProtectionDomain_klass()) {
      is_preservable = false;
      log_debug(cds, heap)(
        "(%d) java.lang.ProtectionDomain object is in the archived sub-graph. "
        "Cannot be preserved.", level);
    } else if (ik->implements_interface(SystemDictionary::Runnable_klass())) {
      is_preservable = false;
      log_debug(cds, heap)(
        "(%d) Object(%s) is Runnable. "
        "Cannot be preserved.", level, k->external_name());
    }
  }

  // Now follow the references and walk the rest of the subgraph.
  if (!has_been_seen_during_subgraph_recording(obj)) {
    set_has_been_seen_during_subgraph_recording(obj);

    if (is_preservable) {
      if (walk_references) {
        WalkOopAndArchiveClosure walker(level, false /* is_closed_region */,
                                        false /* record_klasses_only */,
                                        true  /* check_preservable_only */,
                                        NULL, obj, NULL, THREAD);
        obj->oop_iterate(&walker);
        is_preservable = walker.is_preservable();
      }
    }
  }

  if (!is_preservable) {
    // Propagate the state to the current object if any of the objects within
    // the current object's reachable subgraph is not preservable.
    not_preservable_object_cache()->put(obj, true);
    log_debug(cds, heap)(
              "(%d) %s object subgraph contains not preservable object(s). "
              "Cannot be preserved.",
              level, k->external_name());
  }
  return is_preservable;
}

class PreservableKlassArchiver {
  Thread* _thread;
 public:
  PreservableKlassArchiver(Thread* thread) : _thread(thread) {}

  bool do_entry(Klass *k, bool v) {
    if (k->can_preserve() && k->is_instance_klass()) {
      // The InstanceKlass _init_state is already reset to 'loaded'.
      InstanceKlass* ik = InstanceKlass::cast(k);
      // Only support classes for BUILTIN class loader currently.
      if (ik->shared_classpath_index() >= 0) {
        ResourceMark rm(_thread);
        const char* klass_name = ik->external_name();

        log_info(cds, heap)("Archiving preservable class %s static fields",
                            ik->external_name());

        oop archived_mirror =
          HeapShared::find_archived_heap_object(ik->java_mirror());
        assert(archived_mirror != NULL, "No archived mirror object");

        HeapShared::start_recording_subgraph(ik, klass_name);

        StaticFieldArchiver archiver(ik, archived_mirror, _thread);
        ik->do_local_static_fields(&archiver);

        HeapShared::done_recording_subgraph(ik, klass_name);
      }
    }
    return true;
  }
};

// The current class can be set to
// is_pre_initialized_without_dependency_class if all super types (except
// j.l.Object) have is_pre_initialized_without_dependency_class flags.
//
// At runtime, a class with is_pre_initialized_without_dependency_class
// flag can be set to fully_initialized state immediately after being
// loaded and restored from the shared archive.
bool HeapShared::set_pre_initialize_state(InstanceKlass *ik) {
  if (!ik->can_preserve()) {
    return false;
  };

  InstanceKlass *relocated_ik =
    InstanceKlass::cast(MetaspaceShared::get_relocated_klass(ik));
  if (relocated_ik->has_pre_initialized_flag()) {
    return true;
  }

  ResourceMark rm;
  // First process all super classes.
  InstanceKlass *super_k = InstanceKlass::cast(ik->super());
  if (super_k != SystemDictionary::Object_klass()) {
    if (!set_pre_initialize_state(super_k) ||
      MetaspaceShared::get_relocated_klass(super_k)->is_pre_initialized_with_dependency_class()) {
      // The super class is not pre_initialized or its static initializer
      // has dependency classes.
      relocated_ik->set_is_pre_initialized_with_dependency_class();
      log_info(preinit)(
        "Set %s to is_pre_initialized_with_dependency_class",
        ik->external_name());
      return true;
    }
  }

  // If we get here, all super classes (except j.l.Object) of the current
  // class have is_pre_initialized_without_dependency_class flag set.

  // Now process all local interfaces.
  Array<Klass*>* local_interfaces = ik->local_interfaces();
  if (local_interfaces != NULL) {
    for (int idx = 0; idx < local_interfaces->length(); idx++) {
      InstanceKlass *itf = InstanceKlass::cast(local_interfaces->at(idx));
      if (!set_pre_initialize_state(itf) ||
          MetaspaceShared::get_relocated_klass(itf)->is_pre_initialized_with_dependency_class()) {
        // The super interface is not pre_initialized or its static
        // initializer has dependency classes.
        relocated_ik->set_is_pre_initialized_with_dependency_class();
        log_info(preinit)(
          "Set %s to is_pre_initialized_with_dependency_class",
          ik->external_name());
        return true;
      }
    }
  }

  // If we get here, all super classes (except j.l.Object) and super interfaces
  // of the current class have is_pre_initialized_without_dependency_class flag
  // set.

  // Now process the current class.
  KlassSubGraphInfo* info = HeapShared::get_subgraph_info(ik, false);
  if (info->subgraph_object_klasses() == NULL ||
      info->subgraph_object_klasses()->length() == 0) {
    // Current class initializer has no dependency class.
    relocated_ik->set_is_pre_initialized_without_dependency_class();
    log_info(preinit)(
      "Set %s to is_pre_initialized_without_dependency_class",
      ik->external_name());
  } else {
    relocated_ik->set_is_pre_initialized_with_dependency_class();
    log_info(preinit)(
      "Set %s to is_pre_initialized_with_dependency_class",
      ik->external_name());
  }
  return true;
}

class PreservableKlassStateClosure {
 public:
  PreservableKlassStateClosure() {}

  bool do_entry(Klass *k, bool v) {
    HeapShared::set_pre_initialize_state(InstanceKlass::cast(k));
    return true;
  }
};

void HeapShared::archive_preservable_klass_static_fields_subgraphs(Thread* THREAD) {
  if (_preservable_klasses == NULL) {
    return;
  }
  PreservableKlassArchiver archiver(THREAD);
  _preservable_klasses->iterate(&archiver);

  PreservableKlassStateClosure set_state;
  _preservable_klasses->iterate(&set_state);
}

////////////////////////////////////////////////////////////////

// At dump-time, find the location of all the non-null oop pointers in an archived heap
// region. This way we can quickly relocate all the pointers without using
// BasicOopIterateClosure at runtime.
class FindEmbeddedNonNullPointers: public BasicOopIterateClosure {
  narrowOop* _start;
  BitMap *_oopmap;
  int _num_total_oops;
  int _num_null_oops;
 public:
  FindEmbeddedNonNullPointers(narrowOop* start, BitMap* oopmap)
    : _start(start), _oopmap(oopmap), _num_total_oops(0),  _num_null_oops(0) {}

  virtual bool should_verify_oops(void) {
    return false;
  }
  virtual void do_oop(narrowOop* p) {
    _num_total_oops ++;
    narrowOop v = *p;
    if (!CompressedOops::is_null(v)) {
      size_t idx = p - _start;
      _oopmap->set_bit(idx);
    } else {
      _num_null_oops ++;
    }
  }
  virtual void do_oop(oop *p) {
    ShouldNotReachHere();
  }
  int num_total_oops() const { return _num_total_oops; }
  int num_null_oops()  const { return _num_null_oops; }
};

ResourceBitMap HeapShared::calculate_oopmap(MemRegion region) {
  assert(UseCompressedOops, "must be");
  size_t num_bits = region.byte_size() / sizeof(narrowOop);
  ResourceBitMap oopmap(num_bits);

  HeapWord* p   = region.start();
  HeapWord* end = region.end();
  FindEmbeddedNonNullPointers finder((narrowOop*)p, &oopmap);

  int num_objs = 0;
  while (p < end) {
    oop o = (oop)p;
    o->oop_iterate(&finder);
    p += o->size();
    ++ num_objs;
  }

  log_info(cds, heap)("calculate_oopmap: objects = %6d, embedded oops = %7d, nulls = %7d",
                      num_objs, finder.num_total_oops(), finder.num_null_oops());
  return oopmap;
}

// Patch all the embedded oop pointers inside an archived heap region,
// to be consistent with the runtime oop encoding.
class PatchEmbeddedPointers: public BitMapClosure {
  narrowOop* _start;

 public:
  PatchEmbeddedPointers(narrowOop* start) : _start(start) {}

  bool do_bit(size_t offset) {
    narrowOop* p = _start + offset;
    narrowOop v = *p;
    assert(!CompressedOops::is_null(v), "null oops should have been filtered out at dump time");
    oop o = HeapShared::decode_from_archive(v);
    RawAccess<IS_NOT_NULL>::oop_store(p, o);
    return true;
  }
};

void HeapShared::patch_archived_heap_embedded_pointers(MemRegion region, address oopmap,
                                                       size_t oopmap_size_in_bits) {
  BitMapView bm((BitMap::bm_word_t*)oopmap, oopmap_size_in_bits);

#ifndef PRODUCT
  ResourceMark rm;
  ResourceBitMap checkBm = calculate_oopmap(region);
  assert(bm.is_same(checkBm), "sanity");
#endif

  PatchEmbeddedPointers patcher((narrowOop*)region.start());
  bm.iterate(&patcher);
}

#endif // INCLUDE_CDS_JAVA_HEAP

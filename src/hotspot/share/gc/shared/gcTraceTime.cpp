/*
 * Copyright (c) 2012, 2022, Oracle and/or its affiliates. All rights reserved.
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
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcTraceTime.inline.hpp"
#include "gc/shared/gcTrace.hpp"
#include "logging/log.hpp"
#include "logging/logStream.hpp"
#include "memory/universe.hpp"
#include "runtime/os.hpp"

#include <sys/resource.h>
#include <sys/syscall.h>

void GCTraceTimeLoggerImpl::log_start(Ticks start) {
  _start = start;

  LogStream out(_out_start);

  out.print("%s", _title);
  if (_gc_cause != GCCause::_no_gc) {
    out.print(" (%s)", GCCause::to_string(_gc_cause));
  }
  out.cr();

  if (_log_heap_usage) {
    struct swap_stats {
      unsigned int majflt;
      unsigned int majflt_in_region;
    } stats;
    _heap_usage_before = Universe::heap()->used();
    if (syscall(452, &stats) == 0)
      _majflt_before = stats.majflt;
  }
}

void GCTraceTimeLoggerImpl::log_end(Ticks end) {
  double duration_in_ms = TimeHelper::counter_to_millis(end.value() - _start.value());

  LogStream out(_out_end);

  out.print("%s", _title);

  if (_gc_cause != GCCause::_no_gc) {
    out.print(" (%s)", GCCause::to_string(_gc_cause));
  }

  if (_heap_usage_before != SIZE_MAX) {
    CollectedHeap* heap = Universe::heap();
    struct swap_stats {
      unsigned int majflt;
      unsigned int majflt_in_region;
    } stats;
    size_t used_before_m = _heap_usage_before / M;
    size_t used_m = heap->used() / M;
    size_t capacity_m = heap->capacity() / M;
    size_t majflt = -1;
    // syscall 452
    // int sys_get_swap_stats(struct swap_stats *stats);
    if (syscall(452, &stats) == 0)
      majflt = stats.majflt;
    out.print(" " SIZE_FORMAT "M->" SIZE_FORMAT "M("  SIZE_FORMAT "M) "
              "majflt(" SIZE_FORMAT "->" SIZE_FORMAT ")",
              used_before_m, used_m, capacity_m,
              _majflt_before, majflt);
  }

  out.print_cr(" %.3fms", duration_in_ms);

  if (_log_heap_usage && strstr(_title, "Pause Full") != nullptr) {
    // syscall 455
    // int faulty_page_index(unsigned int mode, unsigned long *indices);
    unsigned long * indices = (unsigned long *)malloc(2048 * sizeof(unsigned long));
    if (indices != NULL) {
      if (syscall(455, 3, indices, 0) == 0) {
        out.print("faulty page index: ");
        for (int i = 0; i < 2048; i++) {
          out.print(SIZE_FORMAT ",", indices[i]);
        }
        out.cr();
      }
      free(indices);
    }
    // reset profile
    syscall(455, 2, NULL, 0);
  }
}

GCTraceCPUTime::GCTraceCPUTime(GCTracer* tracer) :
  _active(log_is_enabled(Info, gc, cpu) ||
          (tracer != nullptr && tracer->should_report_cpu_time_event())),
  _starting_user_time(0.0),
  _starting_system_time(0.0),
  _starting_real_time(0.0),
  _tracer(tracer)
{
  if (_active) {
    bool valid = os::getTimesSecs(&_starting_real_time,
                                  &_starting_user_time,
                                  &_starting_system_time);
    if (!valid) {
      log_warning(gc, cpu)("TraceCPUTime: os::getTimesSecs() returned invalid result");
      _active = false;
    }
  }
}

GCTraceCPUTime::~GCTraceCPUTime() {
  if (_active) {
    double real_time, user_time, system_time;
    bool valid = os::getTimesSecs(&real_time, &user_time, &system_time);
    if (valid) {
      user_time -= _starting_user_time;
      system_time -= _starting_system_time;
      real_time -= _starting_real_time;
      log_info(gc, cpu)("User=%3.2fs Sys=%3.2fs Real=%3.2fs", user_time, system_time, real_time);
      if (_tracer != nullptr) {
        _tracer->report_cpu_time_event(user_time, system_time, real_time);
      }
    } else {
      log_warning(gc, cpu)("TraceCPUTime: os::getTimesSecs() returned invalid result");
    }
  }
}

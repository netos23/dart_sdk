// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/bootstrap_natives.h"

#include "vm/exceptions.h"
#include "vm/native_entry.h"
#include "vm/object.h"

namespace dart {

[[noreturn]] static void ThrowModuleUnsupported() {
  Exceptions::ThrowUnsupportedError("dart:module is not implemented yet.");
  UNREACHABLE();
}

DEFINE_NATIVE_ENTRY(Module_load, 0, 1) {
  // TODO(implementation): Decode ModuleSource and return a Module instance.
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_getValue, 0, 2) {
  // TODO(implementation): Resolve the requested exported value.
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeMethod, 0, 5) {
  // TODO(implementation): Dispatch to the requested instance method.
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeStaticMethod, 0, 6) {
  // TODO(implementation): Dispatch to the requested static method.
  ThrowModuleUnsupported();
}

DEFINE_NATIVE_ENTRY(Module_invokeConstructor, 0, 6) {
  // TODO(implementation): Dispatch to the requested constructor.
  ThrowModuleUnsupported();
}

}  // namespace dart



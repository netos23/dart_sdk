// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "dart:_internal" show patch;

Never _unimplemented(String member) {
  throw UnimplementedError("dart:module is not implemented on dartdevc: $member");
}

@patch
class Module {
  @patch
  factory Module.load(ModuleSource source) => _unimplemented("Module.load");

  @patch
  T getValue<T>(String valueName) => _unimplemented("Module.getValue");

  @patch
  T invokeMethod<T>(
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) => _unimplemented("Module.invokeMethod");

  @patch
  T invokeStaticMethod<T>(
    String className,
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) => _unimplemented("Module.invokeStaticMethod");

  @patch
  T invokeConstructor<T>({
    String? className,
    String? constructorName,
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) => _unimplemented("Module.invokeConstructor");
}


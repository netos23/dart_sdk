// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "dart:_internal" show patch;

@patch
@pragma("vm:entry-point")
class Module {
  @patch
  @pragma("vm:external-name", "Module_load")
  external factory Module.load(ModuleSource source);

  @patch
  @pragma("vm:external-name", "Module_getValue")
  external T getValue<T>(String valueName);

  @patch
  @pragma("vm:external-name", "Module_invokeMethod")
  external T invokeMethod<T>(
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });

  @patch
  @pragma("vm:external-name", "Module_invokeStaticMethod")
  external T invokeStaticMethod<T>(
    String className,
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });

  @patch
  @pragma("vm:external-name", "Module_invokeConstructor")
  external T invokeConstructor<T>({
    String? className,
    String? constructorName,
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });
}


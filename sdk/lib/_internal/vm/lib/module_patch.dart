// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import "dart:_internal" show patch;

@patch
@pragma("vm:entry-point")
class Module {
  @pragma("vm:entry-point")
  int _nativeId = 0;
  
  @patch
  @pragma("vm:external-name", "Module_load")
  external factory Module.load(ModuleSource source);

  @patch
  @pragma("vm:external-name", "Module_getValue")
  external T getValue<T>(String valueName);

  // Internal native: args = positional values, argNames = sorted named arg
  // names (parallel with argValues). See Module_invokeMethod in module.cc.
  @pragma("vm:external-name", "Module_invokeMethod")
  external Object? _invokeMethod(
    String name,
    List<Object?> args,
    List<String> argNames,
    List<Object?> argValues,
  );

  @patch
  T invokeMethod<T>(
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) {
    return _invokeMethod(
      name,
      [...positionalArgs, ...optionalArgs],
      namedArgs.keys.toList(),
      namedArgs.values.toList(),
    ) as T;
  }

  @pragma("vm:external-name", "Module_invokeStaticMethod")
  external Object? _invokeStaticMethod(
    String className,
    String name,
    List<Object?> args,
    List<String> argNames,
    List<Object?> argValues,
  );

  @patch
  T invokeStaticMethod<T>(
    String className,
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) {
    return _invokeStaticMethod(
      className,
      name,
      [...positionalArgs, ...optionalArgs],
      namedArgs.keys.toList(),
      namedArgs.values.toList(),
    ) as T;
  }

  @pragma("vm:external-name", "Module_invokeConstructor")
  external Object? _invokeConstructor(
    String? className,
    String? constructorName,
    List<Object?> args,
    List<String> argNames,
    List<Object?> argValues,
  );

  @patch
  T invokeConstructor<T>({
    String? className,
    String? constructorName,
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  }) {
    return _invokeConstructor(
      className,
      constructorName,
      [...positionalArgs, ...optionalArgs],
      namedArgs.keys.toList(),
      namedArgs.values.toList(),
    ) as T;
  }
}
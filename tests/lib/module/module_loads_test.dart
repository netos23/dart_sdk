// Copyright (c) 2026, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:module';

import 'package:expect/expect.dart';

const String modulePath = String.fromEnvironment('module.path');

void expectModuleError(void Function() callback) {
  try {
    callback();
    Expect.fail('Expected dart:module operation to throw');
  } catch (error) {
    Expect.isTrue(
      error is UnsupportedError ||
          error is UnimplementedError ||
          error is ArgumentError,
      'Unexpected error type: $error',
    );
  }
}

void main() {
  final source = ModuleSource(
    modulePath.isEmpty ? '__missing_module_snapshot__' : modulePath,
  );

  Expect.equals(source, ModuleSource(source.path));
  Expect.equals(source.path.hashCode, source.hashCode);
  Expect.isTrue(source.toString().contains(source.path));

  if (modulePath.isEmpty) {
    // Stage 0 limitation: without a precompiled module image this API is only
    // an explicit loading surface. JIT/non-VM runtimes report unsupported or
    // unimplemented, while precompiled runs without a module path fail loading.
    expectModuleError(() => Module.load(source));
    return;
  }

  final module = Module.load(source);

  Expect.equals('Puppy', module.getValue<String>('puppyName'));
  Expect.equals(42, module.invokeMethod<int>('answer'));
  Expect.equals(44, module.invokeMethod<int>('answer', positionalArgs: [2]));

  final answer = module.lookupFunction<int Function(int)>('answerWithOffset');
  Expect.equals(45, answer(3));

  Expect.isNotNull(
    module.invokeStaticMethod<Object?>(
      'Hero',
      'createHero',
      positionalArgs: ['Batman'],
    ),
  );
  Expect.isNotNull(
    module.invokeConstructor<Object?>(
      className: 'Hero',
      constructorName: 'withPrefix',
      positionalArgs: ['Batman'],
      optionalArgs: ['Dark Knight'],
    ),
  );

  expectModuleError(() => module.lookupFunction<Object?>('__missing_export__'));
}

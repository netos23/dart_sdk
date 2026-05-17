// Copyright (c) 2026, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:test/test.dart';
import 'package:vm/module_abi_manifest.dart';

void main() {
  test('empty manifest round trips with stable non-zero hash', () {
    final manifest = DartModuleAbiManifest.empty();
    final decoded = DartModuleAbiManifest.fromJson(
      jsonDecode(manifest.toJsonString()),
    );

    expect(decoded.hash, manifest.hash);
    expect(decoded.hash, isNonZero);
    expect(decoded.hashHex, matches(RegExp(r'^0x[0-9a-f]{16}$')));
    expect(decoded.semantic, manifest.semantic);
  });

  test('semantic hash is deterministic for map key order', () {
    final left = DartModuleAbiManifest.fromSemantic(<String, Object?>{
      'selectors': <Object?>[
        <String, Object?>{'name': 'foo', 'positionalCount': 1},
      ],
      'libraries': <Object?>[],
    });
    final right = DartModuleAbiManifest.fromSemantic(<String, Object?>{
      'libraries': <Object?>[],
      'selectors': <Object?>[
        <String, Object?>{'positionalCount': 1, 'name': 'foo'},
      ],
    });

    expect(right.hash, left.hash);
    expect(right.toJsonString(), left.toJsonString());
  });

  test('stored hash is validated', () {
    final json = DartModuleAbiManifest.empty().toJson();
    json['hash'] = '0x0000000000000001';

    expect(
      () => DartModuleAbiManifest.fromJson(json),
      throwsA(isA<FormatException>()),
    );
  });

  test('detailed dynamic interface becomes semantic ABI entries', () {
    final manifest = DartModuleAbiManifest.fromDetailedDynamicInterfaceJson(
      <String, Object?>{
        'callable': <Object?>[
          <String, Object?>{
            'library': 'package:api/api.dart',
            'class': 'Processor',
          },
          <String, Object?>{
            'library': 'package:api/api.dart',
            'class': 'Processor',
            'member': 'process',
          },
          <String, Object?>{
            'library': 'package:api/api.dart',
            'member': 'createProcessor',
          },
        ],
        'extendable': <Object?>[
          <String, Object?>{
            'library': 'package:api/api.dart',
            'class': 'Processor',
          },
        ],
        'can-be-overridden': <Object?>[
          <String, Object?>{
            'library': 'package:api/api.dart',
            'class': 'Processor',
            'member': 'process',
          },
        ],
      },
    );

    expect(manifest.semantic['libraries'], [
      {'uri': 'package:api/api.dart'},
    ]);
    expect(manifest.semantic['classes'], [
      {
        'library': 'package:api/api.dart',
        'name': 'Processor',
        'flags': ['callable', 'extendable'],
      },
    ]);
    expect(manifest.semantic['members'], [
      {
        'library': 'package:api/api.dart',
        'name': 'createProcessor',
        'flags': ['callable'],
      },
      {
        'library': 'package:api/api.dart',
        'class': 'Processor',
        'name': 'process',
        'flags': ['callable', 'can-be-overridden'],
      },
    ]);
  });

  test('file IO creates parent directories', () {
    final tempDir = Directory.systemTemp.createTempSync(
      'module_abi_manifest_test',
    );
    try {
      final file = File('${tempDir.path}/nested/app.abi.json');
      final manifest = DartModuleAbiManifest.empty();

      manifest.writeToFileSync(file);

      expect(file.existsSync(), isTrue);
      expect(DartModuleAbiManifest.readFromFileSync(file).hash, manifest.hash);
    } finally {
      tempDir.deleteSync(recursive: true);
    }
  });
}

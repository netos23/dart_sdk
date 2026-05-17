// Copyright (c) 2026, the Dart project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:crypto/crypto.dart';

/// Deterministic host/module ABI manifest envelope for `dart:module`.
///
/// The schema is intentionally conservative. It records semantic ABI entries
/// that the current front-end dynamic-interface annotator can describe, and it
/// reserves deterministic sections for the layout, signature, type, selector,
/// and export records that later precompiler passes will populate.
final class DartModuleAbiManifest {
  static const String kind = 'dart-module-abi';
  static const int formatVersion = 1;

  final Map<String, Object?> semantic;
  final int hash;

  DartModuleAbiManifest._(this.semantic, this.hash);

  factory DartModuleAbiManifest.empty() {
    return DartModuleAbiManifest.fromSemantic(const <String, Object?>{});
  }

  factory DartModuleAbiManifest.fromDetailedDynamicInterfaceJson(
    Map<String, Object?> detailedDynamicInterface,
  ) {
    final libraries = <String>{};
    final classFlags = <_ClassKey, Set<String>>{};
    final memberFlags = <_MemberKey, Set<String>>{};

    void addClassFlag(_ClassKey key, String flag) {
      libraries.add(key.library);
      (classFlags[key] ??= <String>{}).add(flag);
    }

    void addMemberFlag(_MemberKey key, String flag) {
      libraries.add(key.library);
      if (key.className != null) {
        classFlags.putIfAbsent(
          _ClassKey(key.library, key.className!),
          () => <String>{},
        );
      }
      (memberFlags[key] ??= <String>{}).add(flag);
    }

    for (final sectionName in const <String>[
      'callable',
      'extendable',
      'can-be-overridden',
    ]) {
      final section = detailedDynamicInterface[sectionName];
      if (section == null) continue;
      if (section is! List) {
        throw FormatException(
          'dynamic interface section $sectionName must be a list',
        );
      }
      for (final entry in section) {
        if (entry is! Map) {
          throw FormatException(
            'dynamic interface section $sectionName entry must be an object',
          );
        }
        final normalized = _normalizeMap(entry);
        final libraryObject = normalized['library'];
        final classNameObject = normalized['class'];
        final memberObject = normalized['member'];
        if (libraryObject is! String || libraryObject.isEmpty) {
          throw FormatException(
            'dynamic interface section $sectionName entry is missing library',
          );
        }
        if (classNameObject != null && classNameObject is! String) {
          throw FormatException(
            'dynamic interface section $sectionName class must be a string',
          );
        }
        if (memberObject != null && memberObject is! String) {
          throw FormatException(
            'dynamic interface section $sectionName member must be a string',
          );
        }
        final library = libraryObject;
        final className = classNameObject as String?;
        final member = memberObject as String?;
        libraries.add(library);
        if (member == null) {
          if (className != null) {
            addClassFlag(_ClassKey(library, className), sectionName);
          }
        } else {
          addMemberFlag(_MemberKey(library, className, member), sectionName);
        }
      }
    }

    final classEntries =
        classFlags.entries.map((entry) {
            final flags = entry.value.toList()..sort();
            return <String, Object?>{
              'library': entry.key.library,
              'name': entry.key.name,
              'flags': flags,
            };
          }).toList()
          ..sort(_compareManifestEntries);

    final memberEntries =
        memberFlags.entries.map((entry) {
            final flags = entry.value.toList()..sort();
            return <String, Object?>{
              'library': entry.key.library,
              if (entry.key.className != null) 'class': entry.key.className,
              'name': entry.key.name,
              'flags': flags,
            };
          }).toList()
          ..sort(_compareManifestEntries);

    return DartModuleAbiManifest.fromSemantic(<String, Object?>{
      'classes': classEntries,
      'exports': <Object?>[],
      'fields': <Object?>[],
      'libraries':
          (libraries.toList()..sort())
              .map((library) => <String, Object?>{'uri': library})
              .toList(),
      'members': memberEntries,
      'selectors': <Object?>[],
    });
  }

  factory DartModuleAbiManifest.fromSemantic(Map<String, Object?> semantic) {
    final normalized = _canonicalizeSemantic(_normalizeMap(semantic));
    return DartModuleAbiManifest._(
      normalized,
      _hashSemanticPayload(normalized),
    );
  }

  factory DartModuleAbiManifest.fromJson(Object? json) {
    if (json is! Map) {
      throw const FormatException('module ABI manifest must be a JSON object');
    }
    final map = _normalizeMap(json);
    if (map['kind'] != kind) {
      throw const FormatException('unsupported module ABI manifest kind');
    }
    if (map['formatVersion'] != formatVersion) {
      throw const FormatException('unsupported module ABI manifest version');
    }
    final semantic = map['semantic'];
    if (semantic is! Map<String, Object?>) {
      throw const FormatException('module ABI manifest is missing semantic');
    }
    final manifest = DartModuleAbiManifest.fromSemantic(semantic);
    final storedHash = map['hash'];
    if (storedHash is! String) {
      throw const FormatException('module ABI manifest is missing hash');
    }
    final decodedHash = _parseHash(storedHash);
    if (decodedHash != manifest.hash) {
      throw const FormatException('module ABI manifest hash mismatch');
    }
    return manifest;
  }

  static DartModuleAbiManifest readFromFileSync(File file) {
    final json = jsonDecode(file.readAsStringSync());
    return DartModuleAbiManifest.fromJson(json);
  }

  Map<String, Object?> toJson() => <String, Object?>{
    'kind': kind,
    'formatVersion': formatVersion,
    'hash': hashHex,
    'semantic': semantic,
  };

  String toJsonString() =>
      '${const JsonEncoder.withIndent('  ').convert(toJson())}\n';

  void writeToFileSync(File file) {
    file.parent.createSync(recursive: true);
    file.writeAsStringSync(toJsonString(), flush: true);
  }

  String get hashHex => '0x${hash.toRadixString(16).padLeft(16, '0')}';
}

const List<String> _semanticSectionNames = <String>[
  'abiImports',
  'classLayouts',
  'classes',
  'exports',
  'fieldLayouts',
  'fields',
  'hostExports',
  'implementationSlots',
  'libraries',
  'memberSignatures',
  'members',
  'moduleExports',
  'moduleImports',
  'selectorMetadata',
  'selectors',
  'typeArguments',
  'types',
];

Map<String, Object?> _canonicalizeSemantic(Map<String, Object?> semantic) {
  final result = <String, Object?>{
    for (final sectionName in _semanticSectionNames)
      sectionName: semantic[sectionName] ?? <Object?>[],
  };
  for (final entry in semantic.entries) {
    if (!_semanticSectionNames.contains(entry.key)) {
      result[entry.key] = entry.value;
    }
  }
  for (final sectionName in _semanticSectionNames) {
    final section = result[sectionName];
    if (section is! List) {
      throw FormatException(
        'module ABI manifest semantic section $sectionName must be a list',
      );
    }
  }
  return _normalizeMap(result);
}

int _hashSemanticPayload(Map<String, Object?> semantic) {
  final digest = sha256.convert(utf8.encode(_canonicalJson(semantic)));
  var value = digest.bytes[0] & 0x7f;
  for (var i = 1; i < 8; i++) {
    value = value * 256 + digest.bytes[i];
  }
  // Keep the hash in signed 63-bit range because the Dart tooling passes the
  // value through `int`, while zero means "no ABI hash" in the VM.
  return value == 0 ? 1 : value;
}

int _parseHash(String value) {
  final normalized = value.startsWith('0x') ? value.substring(2) : value;
  return int.parse(normalized, radix: value.startsWith('0x') ? 16 : 10);
}

Map<String, Object?> _normalizeMap(Map<Object?, Object?> map) {
  final keys = <String>[];
  for (final key in map.keys) {
    if (key is! String) {
      throw const FormatException('module ABI manifest keys must be strings');
    }
    keys.add(key);
  }
  keys.sort();
  return <String, Object?>{
    for (final key in keys) key: _normalizeValue(map[key]),
  };
}

Object? _normalizeValue(Object? value) {
  if (value == null || value is bool || value is num || value is String) {
    return value;
  }
  if (value is List) {
    return <Object?>[for (final element in value) _normalizeValue(element)];
  }
  if (value is Map) {
    return _normalizeMap(value);
  }
  throw FormatException('unsupported module ABI manifest value: $value');
}

String _canonicalJson(Object? value) {
  if (value is Map<String, Object?>) {
    final keys = value.keys.toList()..sort();
    return '{${keys.map((key) {
      return '${jsonEncode(key)}:${_canonicalJson(value[key])}';
    }).join(',')}}';
  }
  if (value is List) {
    return '[${value.map(_canonicalJson).join(',')}]';
  }
  return jsonEncode(value);
}

int _compareManifestEntries(
  Map<String, Object?> left,
  Map<String, Object?> right,
) {
  int compareStringField(String key) {
    final l = left[key] as String? ?? '';
    final r = right[key] as String? ?? '';
    return l.compareTo(r);
  }

  for (final key in const ['library', 'class', 'name', 'uri']) {
    final result = compareStringField(key);
    if (result != 0) return result;
  }
  return _canonicalJson(left).compareTo(_canonicalJson(right));
}

final class _ClassKey {
  final String library;
  final String name;

  const _ClassKey(this.library, this.name);

  @override
  bool operator ==(Object other) {
    return other is _ClassKey && other.library == library && other.name == name;
  }

  @override
  int get hashCode => Object.hash(library, name);
}

final class _MemberKey {
  final String library;
  final String? className;
  final String name;

  const _MemberKey(this.library, this.className, this.name);

  @override
  bool operator ==(Object other) {
    return other is _MemberKey &&
        other.library == library &&
        other.className == className &&
        other.name == name;
  }

  @override
  int get hashCode => Object.hash(library, className, name);
}

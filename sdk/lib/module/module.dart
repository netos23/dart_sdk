library dart.module;

import 'dart:io';
import 'dart:typed_data';

class ModuleSource {
  @pragma("vm:entry-point")
  final String path;

  ModuleSource(this.path);

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is ModuleSource &&
          runtimeType == other.runtimeType &&
          path == other.path;

  @override
  int get hashCode => path.hashCode;

  @override
  String toString() {
    return 'ModuleSource{path: $path}';
  }
}

class Module {
  external factory Module.load(ModuleSource source);

  /// Get top level value
  external T getValue<T>(String valueName);

  /// Look up an exported top-level function and return it as a callable value.
  external T lookupFunction<T>(String exportName);

  /// Invoke top level method
  external T invokeMethod<T>(
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });

  /// Invoke static method
  external T invokeStaticMethod<T>(
    String className,
    String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });

  /// Invoke constructor
  external T invokeConstructor<T>({
    String? className,
    String? constructorName,
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });
}

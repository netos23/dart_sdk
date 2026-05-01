library dart.module;

import 'dart:io';
import 'dart:typed_data';


sealed class ModuleSource {
  Future<Uint8List> loadData();
}

base class FileModuleSource extends ModuleSource {
  final String path;

  FileModuleSource({required this.path});


  @override
  Future<Uint8List> loadData() {
    return File(path).readAsBytes();
  }

}



class Module {
  external factory Module.load(ModuleSource source);

  /// Get top level value
  external T getValue<T>(String valueName);

  /// Invoke top level method
  external T invokeMethod<T>(String name, {
    List<Object?> positionalArgs = const [],
    List<Object?> optionalArgs = const [],
    Map<String, Object?> namedArgs = const {},
  });

  /// Invoke static method
  external T invokeStaticMethod<T>(String className,
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

import 'dart:module';

void main(){
  final module = Module.load(
    FileModuleSource(path: 'test/lib/module/test_module.dart'),
  );

  final value = module.getValue<String>('testValue');
  print('Value: $value');
}
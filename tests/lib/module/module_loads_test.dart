import 'dart:module';

void main() {
  final module = Module.load(
    ModuleSource(
      '/Volumes/T7/dart_enhancement/wd/sdk/tests/lib/module/simple_module.dylib',
    ),
  );

  final value = module.getValue<String>('puppyName');
  print('Value: $value');

  // dynamic defaultHero = module.invokeConstructor(
  //   className: 'Hero',
  //   constructorName: 'defaultHero',
  // );
  //
  //
  //
  // if(defaultHero == null){
  //   print('Hero missed');
  // }

  final customHero = module.invokeConstructor(
    className: 'Hero',
    constructorName: 'withPrefix',
    positionalArgs: ['Batman'],
    optionalArgs: ['Dark Knight'],
  );

  if(customHero == null){
    print('Hero missed');
  }
  //
  // if(customHero == null){
  //   print('Hero missed');
  // }

  final value1 = module.getValue<String>('puppyName');
  print('Value: $value1');

  // print('Default Hero: ${defaultHero.toString()}');

  // print('Result is ${defaultHero?.greet()}');

  // final trinResult = module.invokeMethod<int>(
  //   'trinHero',
  //   positionalArgs: [defaultHero],
  // );

  // print('Result is $trinResult');
}

import 'dart:math';

@pragma("vm:entry-point")
String puppyName = "Puppy";

@pragma("vm:entry-point")
@pragma("vm:never-inline")
int answer([int offset = 0]) => 42 + offset;

@pragma("vm:entry-point")
@pragma("vm:never-inline")
int answerWithOffset(int offset) => answer(offset);

@pragma("vm:entry-point")
@pragma("vm:never-inline")
int trinHero(Hero hero) {
  print("Trin");
  final iter = Random().nextInt(10);
  for (var i = 0; i < iter; i++) {
    print("Trin $hero, iteration $i");
  }

  return iter;
}

@pragma("vm:entry-point")
@pragma("vm:never-inline")
class Hero {
  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  static Hero createHero(String? name, {bool printName = false}) {
    final nik = name ?? "Superman";

    if (printName) {
      print("Creating hero with name: $nik");
    }

    return Hero(nik);
  }

  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  String name;

  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  Hero(this.name);

  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  factory Hero.defaultHero() {
    return Hero("Default Hero");
  }

  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  Hero.withPrefix(String name, [String prefix = "Hero"])
    : name = "$prefix $name";

  @pragma("vm:never-inline")
  @pragma("vm:entry-point")
  String greet() => "Hello, I am $name!";
}

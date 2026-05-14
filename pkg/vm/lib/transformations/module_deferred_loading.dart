library vm.transformations.deferred_loading;

import 'package:kernel/ast.dart';
import 'package:kernel/target/targets.dart' show Target;

import '../metadata/loading_units.dart';

List<LoadingUnit> computeLoadingUnits(Component component) {
  return [
    new LoadingUnit(
      1,
      0,
      component.libraries.map((lib) => lib.importUri.toString()).toList(),
    ),
  ];
}

Component transformComponent(
  Component component,
  Target target,
) {
  final metadata = new LoadingUnitsMetadata(computeLoadingUnits(component));
  final repo = new LoadingUnitsMetadataRepository();
  component.addMetadataRepository(repo);
  repo.mapping[component] = metadata;

  return component;
}

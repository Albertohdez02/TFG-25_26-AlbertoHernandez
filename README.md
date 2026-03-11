# IHTC 2024 Solver

Solver para el problema de planificación hospitalaria integrada (IHTC 2024 Competition). Asigna pacientes a habitaciones, días de admisión, quirófanos y enfermeras, respetando 13 restricciones duras y minimizando costes blandos ponderados.

**TFG — Alberto Hernández, 4º Ingeniería Informática**

## Requisitos

- CMake >= 3.16
- Compilador C++20 (GCC 11+, Clang 14+)

## Compilación

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

El ejecutable se genera en `build/ihtc_solver`.

## Ejecución

```bash
./build/ihtc_solver <instancia.json> [seed] [max_iter] [restarts]
```

| Parámetro | Descripción | Default |
|-----------|------------|---------|
| `instancia.json` | Fichero JSON con la instancia del problema | (obligatorio) |
| `seed` | Semilla para el generador aleatorio | 42 |
| `max_iter` | Iteraciones máximas de búsqueda local por restart | 5000 |
| `restarts` | Número de reinicios multi-start | 10 |

### Ejemplos

```bash
# Ejecutar una instancia con parámetros por defecto
./build/ihtc_solver data/test01.json

# Con seed personalizada, 10000 iteraciones y 5 restarts
./build/ihtc_solver data/test01.json 12 10000 5

# Ejecutar todas las instancias
for i in $(seq -w 1 10); do
  ./build/ihtc_solver data/test${i}.json
done
```

La solución se exporta automáticamente a `solutions/<nombre_instancia>_solution.json`.

## Estructura del proyecto

```
src/
├── main.cpp                    # Pipeline: multi-start + ILS
├── common/types.h              # Tipos y aliases
├── entities/                   # Entidades del problema
│   ├── Patient.h, Room.h, Nurse.h, Surgeon.h,
│   ├── OperatingTheater.h, Occupant.h, ProblemData.h
├── evaluator/                  # Evaluación y factibilidad
│   ├── Evaluator.h/.cpp        # 12 componentes de coste blando
│   ├── FeasibilityChecker.h/.cpp  # 13 restricciones duras (HC1-HC13)
├── io/                         # Entrada/salida
│   ├── ProblemParser.h         # Parser de instancias JSON
│   ├── SolutionIO.h            # Exportación de soluciones
│   ├── json.hpp                # nlohmann/json
├── solution/
│   ├── Solution.h/.cpp         # Representación de la solución
├── solver/
│   ├── RandomGenerator.h/.cpp  # Generador constructivo greedy
│   ├── LocalSearch.h/.cpp      # ILS con 8 vecindarios
data/                           # Instancias de test (test01-test10)
solutions/                      # Soluciones exportadas
```
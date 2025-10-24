# mp-mesi — Simulador multiprocesador con coherencia MESI (C++20)

**Autor:** Daniel Brenes Gomez  
**Email:** <danbg31@gmail.com>

Simulador docente de un sistema multiprocesador con cachés privadas por núcleo y coherencia **MESI** mediante un **bus compartido con snooping**. Permite cargar un programa “ASM didáctico”, ejecutar en modo normal o **stepping interactivo** (paso a paso), y observar en tiempo real:

- Estados y contenido de **caché por PE** (Processing Element)
- **Memoria principal** (DRAM simulada)
- **Estadísticas del bus** (bytes transferidos, BusRd/BusRdX/Upgr, flushes)
- **Registros** y diffs por PE en cada paso

Incluye un ejemplo de producto punto (dot product) distribuido por 4 PEs.

---

## Tabla de contenidos

- [Estructura del proyecto](#estructura-del-proyecto)
- [Requisitos](#requisitos)
- [Compilación](#compilación)
- [Ejecución](#ejecución)
  - [Modo normal](#modo-normal)
  - [Modo stepping (interactivo)](#modo-stepping-interactivo)
- [Entrada `input.txt`](#entrada-inputtxt)
- [ASM didáctico](#asm-didáctico)
- [Qué se imprime y cómo leerlo](#qué-se-imprime-y-cómo-leerlo)
- [Parámetros y configuración](#parámetros-y-configuración)
- [Extender el simulador](#extender-el-simulador)
- [Limitaciones conocidas](#limitaciones-conocidas)
- [Créditos](#créditos)

---

## Estructura del proyecto

```
.
├── include/
│   ├── assembler.hpp
│   ├── bus.hpp
│   ├── cache.hpp
│   ├── config.hpp
│   ├── memory.hpp
│   ├── processor.hpp
│   ├── simulator.hpp
│   └── types.hpp
├── src/
│   ├── assembler.cpp
│   ├── bus.cpp
│   ├── cache.cpp
│   ├── memory.cpp
│   ├── processor.cpp
│   └── simulator.cpp
├── examples/
│   └── demo.asm
├── main.cpp
├── input.txt
└── Makefile
```

Componentes principales:

- **Memory**: DRAM plana con lecturas/escrituras de 64 bits (double).
- **Cache**: caché privada por PE con protocolo **MESI** (Estados **M/E/S/I**).  
  Soporta BusRd, BusRdX, BusUpgr, invalidaciones y (cuando aplica) flush.
- **Bus**: arbitra peticiones, propaga snoops, contabiliza bytes/flushes/comandos.
- **Processor (PE)**: CPU didáctica que ejecuta un “ASM simple”.
- **Simulator**: orquesta memoria, bus, cachés y PEs; ofrece ejecución normal y **stepping**.
- **Assembler**: traduce `demo.asm` a un programa interno para los PEs.

---

## Requisitos

- **GCC / G++** con soporte **C++20** (probado con GCC 11+ en Linux).
- `make`

> En Ubuntu/Debian: `sudo apt-get install build-essential`

---

## Compilación

```bash
make clean && make
```

Targets útiles:

- `make` — compila el binario `mp-mesi`
- `make run` — ejecuta (usa `examples/demo.asm` por defecto)
- `make runasm` — alias de `make run`
- `make step` — ejecuta en **modo stepping** interactivo
- `make debug` — recompila con `-g -O0`
- `make clean` — limpia `build/` y el binario

---

## Ejecución

### Modo normal

```bash
# usa el demo por defecto
make run

# o con un .asm específico
./mp-mesi path/a/tu_programa.asm
```

Al inicio verás la carga del problema (por defecto dot product) y un **volcado de memoria inicial**. Luego los logs de ejecución (loads/stores, bus, etc.), y al final:

- **Resultado final** del producto punto en `PE0`
- **Métricas** por caché (loads, stores, hits, misses, invalidations, flushes)
- **Estadísticas del bus**
- **Dump** de registros/memoria por PE (post-mortem)

### Modo stepping (interactivo)

```bash
make step
# o:
./mp-mesi --step examples/demo.asm
# o:
STEPPING=1 ./mp-mesi examples/demo.asm
```

Controles:

- **ENTER**: ejecuta **un paso** (cada PE intenta 1 instrucción + 1 avance de bus)
- **c**: continuar automático hasta terminar
- **r**: imprimir registros de todos los PEs
- **b**: resumen del bus
- **q**: salir del stepping

Por cada paso se imprime:

1. **BEFORE** por PE: snapshot de R0..R4 (con R1–R3 como direcciones y R4 en f64)
2. Logs de la **instrucción ejecutada** por los PEs (p.ej. `LOAD/FMUL/FADD/STORE/...`)
3. `[BUS] step()` y **resumen del bus** (bytes, BusRd/X, Upgr, flushes)
4. **REG DIFFS (AFTER)**: sólo los registros que cambiaron (con representación f64 donde aplica)
5. **Dump de caché** por PE (set/way/estado y, si se habilita, datos)

Esto permite depurar con precisión qué hace cada PE en cada instrucción y cómo interactúa el bus/coherencia.

---

## Entrada `input.txt`

El simulador inicializa el dot product leyendo dos líneas de `input.txt`:

- **Línea 1**: valores de `A` (separados por espacios)
- **Línea 2**: valores de `B` (separados por espacios)

Se toma `M = min(len(A), len(B), N)`. Si faltan valores, se rellenan con **0.0** hasta `N`.  
Por defecto el ejemplo usa `N=16` y 4 PEs, por lo que a cada PE le tocan `N/4 = 4` elementos.

Ejemplo de `input.txt`:

```
0.369 1.582 2.743 3.196 4.801 1.321 7.123 2.754 1.422 0.654 1.601 2.967 4.123
1.120 0.532 3.941 2.174 0.864 3.125 2.563 1.124 0.589 1.794 2.421 3.679 6.875
```

---

## ASM didáctico

El archivo `examples/demo.asm` define el bucle de dot product repartido entre los PEs.  
Instrucciones clave (no exhaustivo):

- `MOVI Rd, imm` — carga inmediato
- `LOAD Rd, [Ra]` — lee `double` desde memoria
- `STORE Ra -> [Rd]` — escribe `double`
- `FMUL Rd, Ra, Rb` — multipl. double
- `FADD Rd, Ra, Rb` — suma double
- `INC Rk (+8)` / `DEC Rk` — variaciones de punteros/contadores
- `REDUCE R4 base=Ra count=Rb` — suma en memoria `count` doubles consecutivos a partir de `base` (instrucción asistida para el “reduce” final del dot product)

> Los logs del **Processor** muestran cada instrucción ejecutada por PE, p.ej.:  
> `[PE0] LOAD R5, [R1] @0x0`, `[PE1] FMUL R7, R5, R6`, etc.

---

## Qué se imprime y cómo leerlo

### Logs del Bus y Caché

- **Cola del bus**:
  - `push T#<n> src=<PE> BusRd/BusRdX line=0x<addr> size=32`
  - `proc T#<n> <src> <cmd> line=0x<addr>`
- **Snoops**: cada caché reacciona a eventos del bus
  - `SNOOP PEx cmd=<...> addr=<...> estado=<M/E/S/I> -> Invalidate (I)` o “línea no presente”
- **Contadores**:
  - `bytes` transferidos, `BusRd`, `BusRdX`, `BusUpgr`, `flushes`

### Estados MESI en las cachés

Cada `LOAD/STORE` imprime si fue **hit/miss**, el **set/way** y el **estado** posterior:

- `READ HIT` / `LOAD MISS ... -> BusRd`
- `STORE MISS ... -> BusRdX`
- `WRITE HIT necesita BusUpgr ...`
- Transiciones típicas: `I→E/S/M`, `E→M` (write), `S→I` (invalidate), etc.

### Registros

- `R1`, `R2`, `R3` se interpretan también como **direcciones** en decimal.
- `R4..R7` se muestran además como **double** (`(f64=...)`).
- En stepping, los **DIFFS** muestran sólo los registros que cambiaron, antes → después.

### Validación del resultado

Al final se imprime:

- `[Resultado final en PE0] Producto punto = ...`
- `[Referencia CPU] dot(A,B) ...` — cálculo en CPU para verificar (debe coincidir)

---

## Parámetros y configuración

Revisa `include/config.hpp` para opciones típicas:

- `kNumPEs` — número de PEs (por defecto 4)
- `kWordBytes` — tamaño de palabra (doble, 8B)
- `kMemWords` — cantidad total de palabras de memoria simulada (para el dump)
- Flags de logging (`kLogSim`, etc.)

Direcciones base que usa el simulador (pueden variar según versión):

- `baseA = 0x0`, `baseB = 0x100`, `basePS = 0x200` (en bytes)

---

## Limitaciones conocidas

- Es un **simulador docente**, no un modelo de rendimiento ciclo exacto.
- La **reducción (`REDUCE`)** es una instrucción de apoyo para simplificar la demostración final.
- Sin modelos de latencia/tiempo real; los “ciclos” son lógicos/secuenciales.

---

## Créditos

Proyecto creado por **Daniel Brenes Gomez** (<danbg31@gmail.com>).  
¡Pull requests y sugerencias son bienvenidas!

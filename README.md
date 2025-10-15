# MP-MESI — Simulador base (C++20)

Implementación base y modular para un sistema multiprocesador con coherencia de caché **MESI**.  
Diseñada para servir como **fundación** de las entregas de Semana 4 y posteriores.

## Características

- 4 PEs (configurable) con **caché privada** 2-way, 16 líneas, 32 bytes/ línea.
- **Bus** compartido con difusión de eventos (modelo _snooping_).
- **Memoria** compartida de 512 palabras de 64 bits.
- Políticas **write-allocate** + **write-back**.
- Estados **MESI** con reacciones simplificadas en `snoop()` (invalidate/compartición/flush).
- **Ejecución por ciclos**: primero ejecutan los PEs, luego el Bus procesa la cola.

## Compilación y ejecución

```bash
make
make run
```

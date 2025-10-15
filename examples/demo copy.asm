; demo.asm - Programa de ejemplo para 4 PEs
; Usa REG0 como contador de bucle, y MOVI para configurar direcciones/valores.
; Escenario: escribe 42 en [0x00] y 777 en [0x100], con algunos loads intercalados.

; Inicialización
MOVI REG0, 3        ; contador para el bucle de prueba
MOVI REG1, 0x00     ; dirección A
MOVI REG2, 0x100    ; dirección B
MOVI REG3, 42       ; valor A
MOVI REG4, 777      ; valor B

LOOP:
  LOAD  REG5, [REG1]
  STORE REG3, [REG1]
  LOAD  REG6, [REG2]
  STORE REG4, [REG2]
  DEC   REG0
  JNZ   LOOP

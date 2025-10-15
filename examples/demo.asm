; Producto punto parcial por PE (double, 8 bytes)
; Convención (porque JNZ mira REG0):
;   REG0 = contador (N/kNumPEs)
;   REG1 = puntero a A (segmento de este PE)
;   REG2 = puntero a B (segmento de este PE)
;   REG3 = dirección de partial_sums[PE]

    LOAD  REG4, [REG3]        ; acumulador local

LOOP:
    LOAD  REG5, [REG1]        ; A[i]
    LOAD  REG6, [REG2]        ; B[i]
    FMUL  REG7, REG5, REG6    ; REG7 = A[i]*B[i]
    FADD  REG4, REG4, REG7    ; ACC += REG7
    INC   REG1                ; avanzar 8 bytes (kWordBytes)
    INC   REG2                ; avanzar 8 bytes (kWordBytes)
    DEC   REG0                ; --contador
    JNZ   LOOP

    STORE REG4, [REG3]        ; escribir suma parcial del PE

; ============================================================================
; demo.asm - Trabajo por PE + suma parcial local y reducción final en PE0
; Mantiene la estructura original, sólo se evita traer partial_sums al inicio.
; ============================================================================

        ; REG0 = contador (lo setea el simulador)
        ; REG1 = puntero a A
        ; REG2 = puntero a B
        ; REG3 = &partial_sums[PE]
        ; REG4 = acumulador local (double bits)
        ; REG5 = tmp A
        ; REG6 = tmp B
        ; REG7 = tmp mul

        MOVI    REG4, 0          ; Acumulador = 0.0 (evitar cargar [REG3] con 0.0 a caché)

start:
        LOAD    REG5, [REG1]     ; A[i]
        LOAD    REG6, [REG2]     ; B[i]
        FMUL    REG7, REG5, REG6 ; tmp = A[i]*B[i]
        FADD    REG4, REG4, REG7 ; acc += tmp

        INC     REG1             ; +8
        INC     REG2             ; +8
        DEC     REG0
        JNZ     start

        STORE   REG4, [REG3]     ; Guardar suma parcial local

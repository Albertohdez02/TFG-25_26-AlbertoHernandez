# Ejecuciones en bruto (raw runs) — ACO ajustado con IRACE

Soluciones generadas por el solver en modo `aco`, con la **configuración élite hallada por IRACE**,
empleadas como datos de partida del estudio comparativo. Cada fichero es una ejecución independiente.

## Organización

| Carpeta | Conjunto | Instancias | Ejecuciones |
|---|---|---|---|
| `public-instances/` | público | `i01`–`i30` | 30 × 10 semillas = **300** |
| `hidden-instances/` | oculto | `m01`–`m30` | 30 × 10 semillas = **300** |

- **Semillas**: `7, 13, 31, 42, 99, 123, 256, 777, 1009, 2024`.
- **Presupuesto**: 600 s por ejecución.
- **Nomenclatura**: `<instancia>_s<semilla>.json` (solución en el formato oficial de la competición).

## Configuración IRACE (élite)

```
IHTC_N_ANTS=6           IHTC_VNS_PERTURB_BASE=3
IHTC_ALPHA=1.3247       IHTC_VNS_PERTURB_MAX=5
IHTC_BETA=1.3491        IHTC_VNS_PERTURB_FACTOR=0.1139
IHTC_RHO=0.2026         IHTC_VNS_SWAP_PAIRS=344
IHTC_Q0=0.9413          IHTC_VNS_RELOCATE=133
IHTC_TAU_MIN_FACTOR=12  IHTC_VNS_NURSE_POS=439
IHTC_STAGNATION_K=21    IHTC_VNS_COMPOUND=1
```

## Reproducir una ejecución

```bash
IHTC_N_ANTS=6 IHTC_ALPHA=1.3247 IHTC_BETA=1.3491 IHTC_RHO=0.2026 IHTC_Q0=0.9413 \
IHTC_TAU_MIN_FACTOR=12 IHTC_STAGNATION_K=21 \
IHTC_VNS_PERTURB_BASE=3 IHTC_VNS_PERTURB_MAX=5 IHTC_VNS_PERTURB_FACTOR=0.1139 \
IHTC_VNS_SWAP_PAIRS=344 IHTC_VNS_RELOCATE=133 IHTC_VNS_NURSE_POS=439 IHTC_VNS_COMPOUND=1 \
  ./build/ihtc_solver instances/public-instances/i05.json 42 5000 100 600 aco
```

> Nota: el bucle ACO está acotado por tiempo de reloj, por lo que el resultado a 600 s puede variar
> ligeramente entre máquinas y según la carga del sistema.

## Validar

```bash
./validator/IHTP_Validator instances/public-instances/i05.json raw-runs/public-instances/i05_s42.json
```

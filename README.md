# RM-ELM

A realistic simulator of a fictional **Reduced-Moderation Epithermal
Liquid-Metal reactor**: a 2300 MWt, four-loop, sodium-cooled core moderated by
canned graphite and zirconium-hydride pins, fuelled with (U,Th) carbide
(20% U-235 / 30% U-238 / 50% Th-232 by heavy-metal weight).

Written in C17 with hand-written x86-64 assembly for the hot loops (with
portable C fallbacks that give identical results). The reactor physics comes
from lattice calculations on ENDF/B-VIII.1 nuclear data, not hand-tuned numbers.

## Build

Needs a C compiler (GCC or Clang) and CMake 3.16+.

```sh
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure   # optional: run the test suite
```

On x86-64 the assembly kernels are assembled automatically by the compiler;
the program picks AVX or SSE2 at run time. On ARM or MSVC the C kernels are
used. Force C with `-DRMELM_USE_ASM=OFF`.

## Play (early version)

```sh
./build/rmelm
```

Linux, macOS or WSL terminal. It starts with the core at rated power, held
critical by the shim banks. Only the reactor core exists so far: coolant flow
and inlet temperature are set by hand until the sodium loops are added.

| Command | What it does |
|---|---|
| `ROD <bank> <cm>` | drive a bank to a depth: 0 = fully out, 160 = fully in. Banks: `REG A B C D SAFE ALL` |
| `SCRAM` | manual reactor trip, all 55 rods drop |
| `RESET` | reset a trip (rods stay in; drive them out yourself) |
| `FLOW <%>` | primary sodium flow |
| `TIN <C>` | core inlet sodium temperature |
| `RUN <1-8>` | simulation speed |
| `HELP`, `QUIT` | |

Automatic trips: power above 118%, reactor period shorter than 8 s,
cladding above 700 C, power/flow mismatch.

Things to try: withdraw `REG` a few cm and watch power and fuel temperature
rise until Doppler feedback levels it off; cut `FLOW` and see the trip; SCRAM
and watch decay heat take over from fission power.

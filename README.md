# vtop

Small `top`-style process monitor for Linux, written in C (no external libraries).
Shows load, CPU/memory/swap, PSI pressure, and processes grouped by executable name
with per-group CPU, memory and disk I/O. Developed with AI assistance.

Not related to the npm package of the same name.

## Build

    make
    sudo make install        # PREFIX=/usr/local by default

## Usage

    vtop [-b] [-n N] [-d S] [-p PID,...] [-u USER] [-c] [-o FIELD] [-w COLS] [-G|+G] [-i]

`-b` batch mode, `-n N` iterations, `-d S` delay, `-o` sort field
(`%CPU|%MEM|DISKR|DISKW|PID|TIME+|NAME`), `+G` disables grouping (top-like view).
Run `vtop -h` for all options.

## License

MIT, see `LICENSE`.

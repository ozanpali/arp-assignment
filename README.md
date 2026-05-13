# ARP drone simulation assignment

A small multi-process “drone simulation game” built for an ARP assignment. It uses POSIX pipes for IPC, a named semaphore to serialize logfile writes, and `ncurses` for the on-screen UI.

When you run the master process, it forks the other components and (by default) opens separate **Konsole** windows for interactive components.

## Quick start

### Prerequisites

- Linux toolchain: `gcc` + `make`
- `ncurses` development package (for UI/keyboard/watchdog)
- `konsole` (the master process launches UI/Keyboard/Watchdog using `konsole -e ...`)

Install commands (pick your distro):

```sh
# Ubuntu
sudo apt update
sudo apt install -y build-essential libncurses-dev konsole
```

### Build

The repo already includes `build/` and `log/`, but if you created a fresh copy make sure they exist:

```sh
mkdir -p build log
```

Compile everything:

```sh
make
```

### Run

```sh
./build/master
```

Expected behavior:

- 1 terminal stays running `master`
- 3 Konsole windows open: **UI**, **Keyboard**, **Watchdog**

## Controls (Keyboard window)

The keyboard process sends raw key presses; the drone interprets them as forces:

- `w` / `a` / `s` / `d`: move
- `q` `e` `z` `c`: diagonals
- `x`: reset forces to 0
- `Esc`: exit the drone loop (watchdog detects it and the master terminates the remaining processes)

Notes:

- Controls are **lowercase** (as implemented).
- The UI draws the drone as `+`, obstacles as `X`, and targets as numbers (`0..NUM_TARGETS-1`).

## Components (what each process does)

- **Master** (`master.c`): creates pipes + log semaphore, forks all components, and supervises shutdown.
- **Server** (`server.c`): central router; receives updates from all processes and forwards the shared `struct data`.
- **UI** (`UI.c`): `ncurses` renderer; sends window size + initial centered position; renders drone/targets/obstacles and score.
- **Keyboard** (`keyboard.c`): `ncurses` key capture; sends latest key to the server.
- **Drone** (`drone.c`): updates drone position (Euler-like discrete update) from key forces + obstacle repulsion.
- **Targets** (`targets.c`): generates random targets and marks them as hit when the drone gets close.
- **Obstacles** (`obstacles.c`): periodically generates random obstacles and sends them to the server.
- **Watchdog** (`watchdog.c`): checks that all processes are alive and displays status in `ncurses`.

Shared state lives in `struct data` in `include/constants.h`.

## Logs

All components append to:

- `log/logfile.txt`

Helper make targets:

```sh
make clean-logs   # truncate logfile
make clean        # remove build/*
```

## Troubleshooting

- **`konsole: not found`**: install Konsole (recommended) or change the terminal command in `master.c` to your terminal emulator.
- **Wayland note (GNOME)**: if you see `Warning: Ignoring XDG_SESSION_TYPE=wayland on Gnome...`, it is emitted by Konsole/Qt. `master` sets `QT_QPA_PLATFORM=wayland` under Wayland sessions to avoid it.
- **Nothing shows in UI**: make sure you’re looking at the UI Konsole window (not the master terminal), and that you built successfully with `make`.



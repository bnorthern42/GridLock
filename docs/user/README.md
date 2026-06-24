# ⚡️ GridLock User Documentation

Welcome to the official user documentation for **GridLock**, the next-generation Qt6/C++23 IDE designed specifically for High-Performance Computing (HPC) and MPI debugging.

## 🧭 Navigation

| Guide | Description |
|---|---|
| 🎨 [**Interface & Layout Guide**](./interface_guide.md) | Master the 3-column layout, bottom docks, and Vim-style shortcuts. |
| 🚀 [**Advanced Debugging**](./advanced_debugging.md) | Leverage the Deadlock Detector, FPE Trapper, Memory Exports, and more. |
| 🌐 [**HPC & Remote Workflows**](./hpc_workflows.md) | Configure SLURM, SSH keys, and Spack environments for cluster execution. |

---

## 🧠 Our Philosophy

GridLock was built from the ground up to solve the unique challenges of debugging massive parallel MPI applications, adhering to three core principles:

> **1. Wayland-Native**  
> We bypass X11 legacy overhead. GridLock uses pure Wayland to render dense data streams without tearing.

> **2. Zero-Copy Architecture**  
> Reading memory from 1,000 MPI ranks shouldn't freeze your IDE. We use `process_vm_readv` and shared memory to inspect debuggee state instantly, achieving zero-copy overhead.

> **3. Keyboard-Centric & Chorded**  
> Mouse travel is wasted time. GridLock implements a global Command Pattern with Vim-style chorded shortcuts, allowing you to manipulate windows, toggle breakpoints, and step through code without lifting your hands.

```mermaid
graph TD;
    A[User Action] -->|Vim Chords| B(Command Manager);
    B --> C{Action Type};
    C -->|UI Shift| D[QSplitter Layout];
    C -->|DAP Request| E[Zero-Copy Backend];
    E -->|process_vm_readv| F[(MPI Ranks)];
```

Jump into the [**Interface Guide**](./interface_guide.md) to get started!

---

> 👩‍💻 **Developers:** Looking to build or contribute to GridLock? See the [**Developer Documentation**](../developer/README.md).

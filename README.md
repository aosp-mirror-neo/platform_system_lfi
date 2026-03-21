# Lightweight Fault Isolation (LFI)

This repo contains code needed to compile LFI in Android that is shared across
the various sandboxed libraries.
- boxrt, a set of runtime stub functions that get linked with the sandboxed library.
- allocator, a thread-safe minimal allocator that utilizes spinlocks
- relocator, a minimal loader that does relocations for `-static-pie` that is necessary for lfi-bind

If you want to learn more about LFI, you can find all the information at these links:
- https://www.scs.stanford.edu/~zyedidia/docs/papers/lfi.pdf (Original Conference Paper)
- https://llvm.org/docs/LFI.html (LFI in LLVM)
- https://www.youtube.com/watch?v=S0apBhyZklg (LFI Talk)

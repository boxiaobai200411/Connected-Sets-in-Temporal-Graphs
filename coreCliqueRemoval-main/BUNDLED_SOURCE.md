# Bundled clique-enumeration source

This directory contains the serial k-clique implementation and its graph and utility dependencies used by Enumerate-RG. The root CMake project builds `graph/graph.cpp` and `clique/kCListClique_v6.cpp` as the `core_clique_kcc` library.

The included implementation exposes a callback for each candidate clique. `src/MutualClique.cpp` in the parent project uses this callback to verify temporal connectivity within the selected vertices.

Only dependencies used by the parent project are included. The original `README.txt` and `OUTPUT_GUIDE.md` are retained for reference; their standalone and parallel executable commands refer to the larger original project and are not build targets of this subset. Use the build commands in the root README.

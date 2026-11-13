# compile
    cmake -S ./
    make

# clique output (all four programs)

    bin/kcc -f_txt dataFile -k k -o cliques.txt
    bin/mce -f_txt dataFile -o maximal-cliques.txt

Each output line is: size vertexID... (original input IDs, sorted).
Omit -o to count only. Parallel programs and -e/-steal also support -o.
Windows: run ./build-windows.ps1 to build bin/*.exe.
See OUTPUT_GUIDE.md for examples, format and source locations.

We get bin/mce and bin/kcc now.

# data format and command

## each edge per line

    bin/mce noUVM -f_txt dataFile
    bin/kcc noUVM -f_txt dataFile -k k
    bin/KCCparallel noUVM -f_txt dataFile -k k -steal

## The first line is the number of nodes and edges, then each edge per line

    bin/mce -f_txt dataFile
    bin/kcc -f_txt dataFile -k k
    bin/MCEparallel -f_txt dataFile -k k -t 10 -steal



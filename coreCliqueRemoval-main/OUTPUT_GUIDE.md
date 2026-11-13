# 团枚举结果输出

四个程序均增加 `-o 输出文件`：`kcc`、`KCCparallel` 输出全部大小为 k 的团；`mce`、`MCEparallel` 输出全部极大团（不能再加入其他顶点的团）。不指定 `-o` 时仍只报告数量。

## 直接运行（Windows PowerShell）

在 `coreCliqueRemoval-main` 目录执行，已编译的程序在 `bin`：

```powershell
# 固定大小的团：示例图中共有 5 个三角形。
.\bin\kcc.exe -f_txt .\examples\output-demo.txt -k 3 -o .\examples\triangles.txt

# 极大团：示例图中共有 3 个极大团，包括孤立顶点 5。
.\bin\mce.exe -f_txt .\examples\output-demo.txt -o .\examples\maximal-cliques.txt

# 并行版本（-e 和 -steal 模式也支持输出）。
.\bin\KCCparallel.exe -f_txt .\examples\output-demo.txt -k 3 -t 4 -steal -o .\examples\triangles-parallel.txt
.\bin\MCEparallel.exe -f_txt .\examples\output-demo.txt -t 4 -steal -o .\examples\maximal-cliques-parallel.txt
```

输出文件会覆盖已有同名文件。每行是 `团大小 顶点ID...`，没有总数表头；数量显示在终端的 `written:` 行。成员按 ID 升序排列，使用输入文件的顶点 ID，已还原 core ordering 的内部重编号。并行输出的行顺序可能变化。

示例的极大团输出为以下三行（顺序不限）：

```text
4 0 1 2 3
3 2 3 4
1 5
```

## 输入与编译

沿用项目的静态无向简单图输入：`-f_txt` 第一行 `n m`，随后 m 行 `u v`，每条无向边只出现一次，无自环、无重边。建议节点为 `0..n-1`，可包含孤立点。`noUVM -f_txt` 用于无表头边列表，沿用原程序以最小、最大 ID 确定节点范围的方式。`-f_txtD` 用于每条边的两个方向均已写出的输入；`-f` 用于原项目的二进制 CSR 数据。

这里的输入是静态图，不能直接传入本项目数据集的 `n m T` / `u v t` 时序格式。

Windows 重新编译（需要 PATH 中有支持 OpenMP 的 MinGW `g++`）：

```powershell
.\build-windows.ps1
python .\tests\verify_clique_output.py
```

Linux 可继续用原 CMake 构建方式，建议单独建目录以避开仓库内旧的 CMake 缓存：

```sh
cmake -S . -B build
cmake --build build -j
./build/bin/kcc -f_txt examples/output-demo.txt -k 3 -o examples/triangles.txt
```

## 修改位置

- `src/tools/cliqueOutput.hpp`：统一文件输出，保证多个线程不会交错写入同一行。
- `src/clique/kCListClique_v6.cpp`：固定 k 团的递归路径和组合枚举，原来 `CN` 一次累加的部分在输出模式下展开。
- `src/clique/parallelkCListClique.cpp`：固定 k 团的并行输出，含线程独立路径及边并行组合分支。
- `src/MClique/mcecrrv3.cpp`：`emitClique()` 输出根节点、R 和 C 中的完整极大团。
- `src/MClique/parallelCCRMCE.cpp`：极大团的并行输出，包含边并行模式的双根及预处理结果。
- `src/run/` 下四个入口：解析 `-o`、连接输出器、打印写入数量；`src/graph/graph.h` 的 `originalId()` 还原输入编号。

输出按枚举过程流式写盘，不在内存积攒全部结果。输出模式的耗时包含组合展开、成员排序和文件写入，论文中比较性能时应统一是否开启输出。

## 从根项目的 src/main.cpp 调用

根目录 `CMakeLists.txt` 已增加 `core_clique_kcc` 库，并将其链接到 `BlackHoleSCC`。重新加载 CMake、编译主程序后，根目录 `src/main.cpp` 可以直接 `#include "kCListClique.h"`，调用示例在 `runKCliqueFromFile()` 中：

```cpp
Graph cliqueGraph;
cliqueGraph.readFromText(inputFile);   // 静态无向图：n m，然后每行 u v
cliqueGraph.changeToCoreOrder();

CliqueOutput output(outputFile);
kclistClique enumerator(std::move(cliqueGraph), k);
enumerator.setCliqueCallback([&](const std::vector<ui>& vertices) {
    // 每次收到一个完整 k 团；在这里打印、保存或继续验证。
    output.emit(vertices);
});
const double count = enumerator.run_v6();
output.finish();
```

主程序新增的命令行入口（工作目录为项目根目录）：

```powershell
.\tmp\core_clique_integration_build\BlackHoleSCC.exe -clique .\coreCliqueRemoval-main\examples\output-demo.txt -k 3 -o .\tmp\main-cliques.txt
```

也可以用 CLion 构建出的 `BlackHoleSCC` 执行同样的参数。省略 `-o` 时只计数；原有 `-d 数据集 -k 大小 -p 百分比` 继续运行时序查询。

`setCliqueCallback()` 用于调用方逐团处理结果，不要求先创建 `CliqueOutput`。例如只需在屏幕打印，可把回调中的语句换成：

```cpp
for (ui vertex : vertices) std::cout << vertex << ' ';
std::cout << '\n';
```

回调优先于类内的文件输出。根 main 使用此接口，保留了 `emitCurrentClique()` 中你注释掉的原文件输出语句。

如果调用处已经有内存中的无向边列表，可以用下面的构图步骤替换 `readFromText()`，无需先写入文件：

```cpp
Graph cliqueGraph;
cliqueGraph.n = vertexCount;            // 顶点编号必须是 0..vertexCount-1
cliqueGraph.m = static_cast<ui>(edges.size());
cliqueGraph.edges = edges;             // std::vector<Pair>，每条无向边一次，无自环重边
cliqueGraph.buildCSR();
cliqueGraph.changeToCoreOrder();
// 后续同上：构造 kclistClique，再调用 run_v6()。
```

`TemporalSCCs` 与这里的 `Graph` 是不同的类。调用这个接口需要静态无向边；时序边的时间和方向如何参与构图，应由具体的时序算法确定。

## 时序查询对比实验：互达图 + CCR

根项目主程序在每个窗口的 V1 后执行 `graph.runQueryMutualClique(left, right, k)`。继续使用原来的数据集和查询文件参数即可，例如：

```powershell
BlackHoleSCC.exe -d sx-askubuntu -k 5 -p 20
```

实现位于根目录 `src/MutualClique.cpp`，声明在 `src/TemporalSCCs.h`，结果由 `src/main.cpp` 输出。每次查询重新构建窗口、提取大小至少为 k 的静态 SCC，在每个分量内调用现有 `buildMutualReachabilityGraph()`，将返回的局部邻接表直接转换为 CCR 的 CSR 输入，再运行 `kclistClique::run_v6()`。不需要读写中间图文件。

开启了逐团回调：展开组合分支后，把每个团的局部编号还原成原节点编号，再逐源检查该团的诱导时序子图。路径只允许经过这 k 个点，验证通过才累加答案数。每个查询独立统计，终端显示：

```text
V1: ... ms 答案数: ...
Mutual-CCR: ... ms 答案数: ...
```

终端只输出该方法的总耗时和答案数，不输出各阶段时间、图规模和 CCR 初始化日志。总时间包含窗口和静态 SCC 预处理、互达图构建、CSR 转换、core 排序、枚举器初始化、实际团枚举、团内时序互达验证及临时数据释放，不包含原始数据文件的首次加载。各阶段的详细统计仍可从方法返回值读取。

返回值中 `cliqueCount` 保留互达图候选团数，`answerCount` 是验证通过的时序 SCC 数，main 输出后者，与 V1 使用相同的答案定义。例如四点同时间有向环的互达图为 K4，有四个三角形，但每个诱导三点子图都断环；候选数为 4，验证后的答案数为 0，与 V1 相同。新方法不覆盖 `getAnswerCount()` 中的 V1 结果。

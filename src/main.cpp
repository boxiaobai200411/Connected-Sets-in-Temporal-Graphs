#include "TemporalSCCs.h"

#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#ifndef TCS_ENUM_DATASET_DIR
#define TCS_ENUM_DATASET_DIR "../dataset"
#endif

std::string dataid = "CollegeMsg";
ui percent = 20;
ui k = 4;
constexpr ui decompositionAlpha = 10;

int main(int argc,char** argv) {
    if (argc % 2 == 0) {
        std::cerr << "Usage: TCS-Enum [-d <dataid>] [-k <k>] [-p <percent>]\n";
        return 1;
    }

    for (int i = 1; i < argc; i += 2) {
        const std::string option = argv[i];
        const std::string value = argv[i + 1];
        if (option == "-d") dataid = value;
        else if (option == "-k" || option == "-p") {
            ui& parameter = option == "-k" ? k : percent;
            const auto parsed = std::from_chars(value.data(),value.data() + value.size(),parameter);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                std::cerr << "Invalid value for " << option << ": " << value << '\n';
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << option << '\n';
            return 1;
        }
    }
    if (dataid.empty() || k < 2) {
        std::cerr << "Specify -d <dataid>, -k <k >= 2>, -p <percent>\n";
        return 1;
    }

    const std::string dataPath = std::string(TCS_ENUM_DATASET_DIR) + "/";
    const std::string queryPath = dataPath + dataid + "_query/" + std::to_string(percent) + ".txt";
    std::ifstream input(queryPath);
    if (!input) {
        std::cerr << "Cannot open query file: " << queryPath << '\n';
        return 1;
    }

    TemporalSCCs graph(dataPath,dataid);
    graph.setDecompositionAlpha(decompositionAlpha);
    std::cout << "dataset=" << dataid << " k=" << k << " percent=" << percent
              << " alpha=" << decompositionAlpha << '\n';

    ui left,right;
    bool ranQuery = false;
    while (input >> left) {
        if (!(input >> right) || left > right || right >= graph.getTimeCount()) {
            std::cerr << "Invalid query interval: " << queryPath << '\n';
            return 1;
        }
        ranQuery = true;

        const auto start1 = std::chrono::steady_clock::now();
        const ul comAnswers = graph.enumerateCom(left,right,k);
        const double comTime = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start1).count();
        std::cout << "Enumerate-Com: " << comTime << " ms answers: " << comAnswers << std::endl;

        const auto start2 = std::chrono::steady_clock::now();
        const ul rgAnswers = graph.enumerateRG(left,right,k);
        const double rgTime = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start2).count();
        std::cout << "Enumerate-RG: " << rgTime << " ms answers: " << rgAnswers << std::endl;

        const auto start3 = std::chrono::steady_clock::now();
        const ul decAnswers = graph.enumerateDec(left,right,k);
        const double decTime = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start3).count();
        std::cout << "Enumerate-Dec: " << decTime << " ms answers: " << decAnswers << std::endl;

        if (comAnswers != rgAnswers || comAnswers != decAnswers) {
            std::cerr << "Answer counts differ for window=[" << left << ',' << right << "]\n";
            return 2;
        }
        std::cout << '\n';
    }
    if (!input.eof() || !ranQuery) {
        std::cerr << "Invalid or empty query file: " << queryPath << '\n';
        return 1;
    }
    return 0;
}

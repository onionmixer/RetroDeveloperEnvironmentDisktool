#include "rdedisktool/CLI.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        rde::CLI cli;
        return cli.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error\n";
        return 1;
    }
}

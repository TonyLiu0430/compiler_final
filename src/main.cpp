#include <iostream>

#include "parser/token.h"

void assert(bool b) {
    if (!b) {
        throw std::logic_error("assert failed");
    }
}

int main() {
    c9ay::Char_constant::match(R"('H')");
    try {
        c9ay::Char_constant::match(R"("H")");
        std::cout << "err" << std::endl;
    } catch (...) {
    }
}

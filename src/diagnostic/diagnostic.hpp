#pragma once

#include <string>
#include <vector>

namespace c9ay {
class Diagnostic {
public:
    struct Ctx {
        int pos;
        std::string &error_msg;
    };
    void report_error(Ctx ctx) {
        errors.push_back(ctx);
    }

private:
    std::vector<Ctx> errors;
};
}  // namespace c9ay
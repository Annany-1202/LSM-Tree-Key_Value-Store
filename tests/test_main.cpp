#include "test_framework.h"

int main(int argc, char** argv) {
    std::string filter = "";
    if (argc > 1) {
        filter = argv[1];
    }
    return test_framework::TestRegistry::instance().runAll(filter);
}

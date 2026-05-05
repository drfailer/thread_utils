#include <cstdio>
#include "log.hpp"


// TODO(LOGGER): printf should be replace with a logger
bool ptr_arg_check_(void *arg, const char *arg_name, const char *function_name, size_t line) {
    if (arg != nullptr) {
        return true;
    }
    printf("[TU_ERROR]: null argument `%s' found in %s(%ld).\n", arg_name, function_name, line);
    return false;
}

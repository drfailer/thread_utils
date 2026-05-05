#ifndef THREAD_UTILS_DFG_LOG
#define THREAD_UTILS_DFG_LOG

#define ptr_arg_check(arg) ptr_arg_check_((void *)(arg), #arg, __FUNCTION__, __LINE__)
bool ptr_arg_check_(void *arg, const char *arg_name, const char *function_name, size_t line);

#endif

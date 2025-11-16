#pragma once

#ifdef __cplusplus
#  include <atomic>     // для C++
#  define ATOMIC_BOOL   std::atomic_bool
#  define ATOMIC_SIZE_T std::atomic_size_t
#  define ATOMIC_ULLONG std::atomic_ullong
#else
#  include <stdatomic.h> 
#  define ATOMIC_BOOL   atomic_bool
#  define ATOMIC_SIZE_T atomic_size_t
#  define ATOMIC_ULLONG atomic_ullong
#endif

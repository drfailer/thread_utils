#ifndef SRC_THREAD_UTILS_DFG_TYPE
#define SRC_THREAD_UTILS_DFG_TYPE

struct TU_TypeRegister;

// Memory allocation:
//
// Functins that should be specified when using the communicator task (the
// receiver needs to allocate data). It can also be used in the standard tasks.
//
// TODO: do we want to optionaly store the pointers somewhere and do some reference counting?

using TU_AllocProc = void *(*)(void *, TU_TypeId);
using TU_FreeProc = void (*)(void *, void *, TU_TypeId);

// Packing
//
// Packing is used for multi-node programs in which data needs to be exchanged
// with other processes.
//
// Packing is done using a package type which is a collection of multiple
// buffers. Most of the time, only one buffer is needed (one pointer to the
// data and the size), but having multiple buffers makes things easier in some
// cases (ex: matrix sent using 1 CPU buffer for the meta-data + 1 GPU buffer
// for the data). Each buffer is sent separately on the network, and the total
// number of buffers is limited to a small amount (TU_MAX_PACKAGE_BUFFER_COUNT)
// as we don't expect users to send too many buffers per data (this can cause
// latency).

struct TU_Buffer {
    char  *data;
    size_t size;
};

#define TU_MAX_PACKAGE_BUFFER_COUNT 4
struct TU_Package {
    TU_Buffer buffers[TU_MAX_PACKAGE_BUFFER_COUNT];
    size_t buffer_count;
};

using TU_PackProc = void (*)(void *, TU_TypeId, TU_Package *);
using TU_UnpackProc = void (*)(void *, TU_TypeId, TU_Package *);
using TU_PackageProc = void (*)(void *, TU_TypeId, TU_Package *);

bool tu_dfg_package_add_buffer(TU_Package *package, char *data, size_t type);

// Type Registry
//
// All types used within the graph must be registered.

struct TU_TypeRegistry {
    TU_Array<TU_TypeRegister> registers;
    size_t type_count;
    bool can_register_more_types; // we don't want to add more types after the first graph nodes are created because it can cause issues
};

struct TU_TypeRegister {
    const char *name;
    TU_TypeId id;
    size_t size; // sizeof used for memcpy

    // Memory management
    TU_AllocProc alloc;     // used to allocate data in the pool
    TU_FreeProc free;       // used to free data from the pool
    void *allocator_data;   // optional data for the allocator

    // Packing procs used with the communicator tasks
    TU_PackProc pack;       // packing callback if serialization needed
    TU_UnpackProc unpack;   // unpack if deserializqtion needed
    TU_PackageProc package; // package memory for network reception (simple type will return a pointer to themselves)
};

TU_TypeId tu_new_type(TU_Dfg *dfg, char const *name, size_t size);
bool tu_type_alloc(TU_Dfg *dfg, TU_TypeId type, TU_AllocProc alloc, TU_FreeProc free);
bool tu_type_pack(TU_Dfg *dfg, TU_TypeId type, TU_PackProc pack, TU_UnpackProc unpack, TU_PackageProc package);

#endif

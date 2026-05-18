#include "type.hpp"

/******************************************************************************/
/*                                  registry                                  */
/******************************************************************************/

TU_TypeId tu_new_type(TU_Dfg *dfg, char const *name, size_t size) {
    TU_TypeId type = dfg->type_registry.type_count;
    dfg->type_registry.registers.emplace_back();
    dfg->type_registry.registers[type].name = name; // TODO: this should be allocated somewhere
    dfg->type_registry.registers[type].size = size;
    dfg->type_registry.type_count += 1;
    return type;
}

bool tu_type_alloc(TU_Dfg *dfg, TU_TypeId type, void allocator_data, TU_AllocProc alloc, TU_FreeProc free) {
    if (type >= dfg->type_registry.type_count) {
        printf("[TU_ERROR]: tu_type_alloc failed, type with id `%d' is not registerd.\n", type);
        return false;
    }
    dfg->type_registry.registers[type].alloc = alloc;
    dfg->type_registry.registers[type].free = free;
    dfg->type_registry.registers[type].allocator_data = allocator_data;
    return true;
}

bool tu_type_pack(TU_Dfg *dfg, TU_TypeId type, TU_PackProc pack, TU_UnpackProc unpack, TU_PackageProc package) {
    if (type >= dfg->type_registry.type_count) {
        printf("[TU_ERROR]: tu_type_pack failed, type with id `%d' is not registerd.\n", type);
        return false;
    }
    dfg->type_registry.registers[type].pack = pack;
    dfg->type_registry.registers[type].unpack = unpack;
    dfg->type_registry.registers[type].package = package;
    return true;
}

/******************************************************************************/
/*                                  package                                   */
/******************************************************************************/

bool tu_dfg_package_add_buffer(TU_Package *package, char *data, size_t type) {
    if (package->buffer_count >= TU_MAX_PACKAGE_BUFFER_COUNT) {
        printf("[TU_ERROR]: cannot add buffer to package `%p' for type `%ld', maximum buffer count reached.\n", package, type);
        return false;
    }
    package->buffers[package->buffer_count++].data = data;
    package->buffers[package->buffer_count++].size = size;
    return true;
}

// The part of the IL2CPP runtime API (GameAssembly.dll's il2cpp_* exports) that
// EpochPact uses, resolved by name at runtime. Every game object is an opaque pointer:
// classes, methods, fields and types are only ever passed back to these functions.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ep::il2cpp {

using Domain = void;
using Assembly = void;
using Image = void;
using Class = void;
using Method = void;
using Field = void;
using Property = void;
using Type = void;

// name, return type, parameters
#define EP_IL2CPP_API(X)                                                                  \
    X(domain_get, Domain*, ())                                                            \
    X(domain_get_assemblies, const Assembly**, (const Domain*, size_t*))                  \
    X(thread_attach, void*, (Domain*))                                                    \
    X(thread_current, void*, ())                                                          \
    X(thread_detach, void, (void*))                                                       \
    X(assembly_get_image, const Image*, (const Assembly*))                                \
    X(image_get_name, const char*, (const Image*))                                        \
    X(image_get_class_count, size_t, (const Image*))                                      \
    X(image_get_class, const Class*, (const Image*, size_t))                              \
    X(class_get_name, const char*, (const Class*))                                        \
    X(class_get_namespace, const char*, (const Class*))                                   \
    X(class_get_parent, const Class*, (const Class*))                                     \
    X(class_get_declaring_type, const Class*, (const Class*))                             \
    X(class_is_enum, bool, (const Class*))                                                \
    X(class_is_valuetype, bool, (const Class*))                                           \
    X(class_is_interface, bool, (const Class*))                                           \
    X(class_is_generic, bool, (const Class*))                                             \
    X(class_get_flags, int, (const Class*))                                               \
    X(class_enum_basetype, const Type*, (const Class*))                                   \
    X(class_get_methods, const Method*, (const Class*, void**))                           \
    X(class_get_fields, const Field*, (const Class*, void**))                             \
    X(class_get_properties, const Property*, (const Class*, void**))                      \
    X(class_from_name, const Class*, (const Image*, const char*, const char*))            \
    X(class_get_method_from_name, const Method*, (const Class*, const char*, int))        \
    X(method_get_name, const char*, (const Method*))                                      \
    X(method_get_param_count, uint32_t, (const Method*))                                  \
    X(method_get_param, const Type*, (const Method*, uint32_t))                           \
    X(method_get_param_name, const char*, (const Method*, uint32_t))                      \
    X(method_get_return_type, const Type*, (const Method*))                               \
    X(method_get_flags, uint32_t, (const Method*, uint32_t*))                             \
    X(type_get_name, char*, (const Type*))                                                \
    X(type_get_type, int, (const Type*))                                                  \
    X(field_get_name, const char*, (const Field*))                                        \
    X(field_get_type, const Type*, (const Field*))                                        \
    X(field_get_offset, size_t, (const Field*))                                           \
    X(field_get_flags, int, (const Field*))                                               \
    X(field_static_get_value, void, (const Field*, void*))                                \
    X(property_get_name, const char*, (const Property*))                                  \
    X(property_get_get_method, const Method*, (const Property*))                          \
    X(property_get_set_method, const Method*, (const Property*))                          \
    X(class_get_field_from_name, const Field*, (const Class*, const char*))                 \
    X(object_new, void*, (const Class*))                                                    \
    X(object_get_class, const Class*, (void*))                                              \
    X(gchandle_new, uintptr_t, (void*, bool))                                             \
    X(gchandle_get_target, void*, (uintptr_t))                                            \
    X(gchandle_free, void, (uintptr_t))                                                   \
    X(free, void, (void*))

struct Api {
#define EP_FIELD(name, ret, params) ret(*name) params = nullptr;
    EP_IL2CPP_API(EP_FIELD)
#undef EP_FIELD
    uintptr_t base = 0;  // GameAssembly.dll's load address
    size_t size = 0;     // and its image size, for RVAs
};

// The resolved table; valid after Resolve() returned true.
const Api& api();

// Resolves every function above from GameAssembly.dll. Fills `missing` with the first
// name that is not exported (so a game update that renames one is reported, not crashed on).
bool Resolve(const char** missing);

// Metadata constants the dump reads (ECMA-335 attributes, as IL2CPP keeps them).
constexpr uint32_t kMethodStatic = 0x0010;
constexpr uint32_t kMethodVirtual = 0x0040;
constexpr uint32_t kMethodAbstract = 0x0400;
constexpr int kFieldStatic = 0x0010;
constexpr int kFieldLiteral = 0x0040;
constexpr int kTypeAttrInterface = 0x0020;

}  // namespace ep::il2cpp

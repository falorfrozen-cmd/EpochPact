// Research dump: every assembly, class, field, property and method the game's IL2CPP
// runtime knows, with method RVAs inside GameAssembly.dll and enum member values.
// Read-only: nothing here runs game code (static constructors included), and a class
// that faults while being read is skipped and counted, never fatal.

#include "dumper.hpp"

#include "common.hpp"
#include "il2cpp_api.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace ep {

namespace {

using namespace il2cpp;

struct Out {
    FILE* cs = nullptr;       // dump.cs: C#-like listing
    FILE* methods = nullptr;  // methods.tsv
    FILE* fields = nullptr;   // fields.tsv
    size_t classes = 0, methodsN = 0, fieldsN = 0, faulted = 0;
    int nameOffset = -1;      // where MethodInfo keeps its name pointer (layout check)
};

std::string TypeName(const Type* t) {
    if (!t) return "?";
    char* raw = api().type_get_name(t);
    if (!raw) return "?";
    std::string s(raw);
    api().free(raw);
    return s;
}

const char* Safe(const char* s) { return s ? s : ""; }

// "Outer.Inner" for a nested class, "Name" otherwise.
std::string ClassPath(const Class* k) {
    std::string name = Safe(api().class_get_name(k));
    for (const Class* outer = api().class_get_declaring_type(k); outer; outer = api().class_get_declaring_type(outer))
        name = std::string(Safe(api().class_get_name(outer))) + "." + name;
    return name;
}

std::string Rva(const void* p) {
    char buf[32];
    auto a = reinterpret_cast<uintptr_t>(p);
    if (!a) return "-";
    if (a >= api().base && a < api().base + api().size)
        std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(a - api().base));
    else
        std::snprintf(buf, sizeof buf, "abs:0x%llX", static_cast<unsigned long long>(a));
    return buf;
}

// IL2CPP keeps a method's code pointer first in MethodInfo; the name pointer's slot
// tells which layout this build uses, and is logged once as a check.
void CheckLayout(Out& o, const Method* m) {
    if (o.nameOffset >= 0 || !m) return;
    const char* name = api().method_get_name(m);
    auto slots = reinterpret_cast<const void* const*>(m);
    for (int i = 1; i < 8; ++i)
        if (slots[i] == name) { o.nameOffset = i * 8; break; }
    Log("dump: MethodInfo name pointer at +%d (code pointer at +0)", o.nameOffset);
}

long long EnumValue(const Class* k, const Field* f) {
    unsigned long long raw = 0;
    api().field_static_get_value(f, &raw);
    switch (api().type_get_type(api().class_enum_basetype(k))) {
        case 0x04: return static_cast<signed char>(raw);   // I1
        case 0x06: return static_cast<short>(raw);         // I2
        case 0x08: return static_cast<int>(raw);           // I4
        case 0x05: return static_cast<unsigned char>(raw); // U1
        case 0x07: return static_cast<unsigned short>(raw);// U2
        case 0x09: return static_cast<unsigned int>(raw);  // U4
        default: return static_cast<long long>(raw);       // I8/U8
    }
}

void DumpClass(Out& o, const char* image, const Class* k) {
    const Api& a = api();
    const std::string ns = Safe(a.class_get_namespace(k));
    const std::string path = ClassPath(k);
    const int flags = a.class_get_flags(k);
    const bool isEnum = a.class_is_enum(k);
    const char* kind = a.class_is_interface(k) ? "interface" : isEnum ? "enum" : a.class_is_valuetype(k) ? "struct" : "class";
    const Class* parent = a.class_get_parent(k);

    std::fprintf(o.cs, "\n// %s\n%s%s%s %s%s%s%s%s\n{\n", image, (flags & 0x80) && !a.class_is_interface(k) ? "abstract " : "",
                 (flags & 0x100) ? "sealed " : "", kind, ns.empty() ? "" : ns.c_str(), ns.empty() ? "" : ".", path.c_str(),
                 parent ? " : " : "", parent ? ClassPath(parent).c_str() : "");

    void* it = nullptr;
    while (const Field* f = a.class_get_fields(k, &it)) {
        const int ff = a.field_get_flags(f);
        const bool isStatic = (ff & kFieldStatic) != 0, literal = (ff & kFieldLiteral) != 0;
        const std::string type = TypeName(a.field_get_type(f));
        const char* fname = Safe(a.field_get_name(f));
        char value[32] = "";
        if (isEnum && literal) std::snprintf(value, sizeof value, "%lld", EnumValue(k, f));
        if (literal)
            std::fprintf(o.cs, "    const %s %s = %s;\n", type.c_str(), fname, value[0] ? value : "?");
        else
            std::fprintf(o.cs, "    %s%s %s; // 0x%zX\n", isStatic ? "static " : "", type.c_str(), fname, a.field_get_offset(f));
        std::fprintf(o.fields, "%s\t%s\t%s\t%s\t%s\t%s\t0x%zX\t%s\n", image, ns.c_str(), path.c_str(), fname, type.c_str(),
                     literal ? "const" : isStatic ? "static" : "instance", literal ? 0 : a.field_get_offset(f), value);
        ++o.fieldsN;
    }

    it = nullptr;
    while (const Property* p = a.class_get_properties(k, &it)) {
        const Method* g = a.property_get_get_method(p);
        const Method* s = a.property_get_set_method(p);
        const Type* t = g ? a.method_get_return_type(g) : nullptr;
        std::fprintf(o.cs, "    %s %s { %s%s}\n", t ? TypeName(t).c_str() : "?", Safe(a.property_get_name(p)), g ? "get; " : "",
                     s ? "set; " : "");
    }

    it = nullptr;
    while (const Method* m = a.class_get_methods(k, &it)) {
        CheckLayout(o, m);
        uint32_t iflags = 0;
        const uint32_t mf = a.method_get_flags(m, &iflags);
        const uint32_t n = a.method_get_param_count(m);
        std::string params, types;
        for (uint32_t i = 0; i < n; ++i) {
            const std::string t = TypeName(a.method_get_param(m, i));
            if (i) { params += ", "; types += ","; }
            params += t + " " + Safe(a.method_get_param_name(m, i));
            types += t;
        }
        const std::string ret = TypeName(a.method_get_return_type(m));
        const std::string rva = Rva(*reinterpret_cast<const void* const*>(m));
        const char* mname = Safe(a.method_get_name(m));
        std::fprintf(o.cs, "    %s%s%s %s(%s); // RVA %s\n", (mf & kMethodStatic) ? "static " : "",
                     (mf & kMethodAbstract) ? "abstract " : (mf & kMethodVirtual) ? "virtual " : "", ret.c_str(), mname,
                     params.c_str(), rva.c_str());
        std::fprintf(o.methods, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", image, ns.c_str(), path.c_str(), mname,
                     (mf & kMethodStatic) ? "static" : "instance", types.c_str(), ret.c_str(), rva.c_str());
        ++o.methodsN;
    }
    std::fputs("}\n", o.cs);
    ++o.classes;
}

bool DumpClassGuarded(Out& o, const char* image, const Class* k) {
    __try {
        DumpClass(o, image, k);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

FILE* OpenOut(const std::wstring& dir, const wchar_t* name) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, (dir + name).c_str(), L"wb") != 0) return nullptr;
    std::setvbuf(f, nullptr, _IOFBF, 1 << 20);
    return f;
}

}  // namespace

bool RunDump(il2cpp::Domain* domain, const std::wstring& dir) {
    const Api& a = api();
    if (!EnsureDir(dir)) {
        Log("dump: cannot create the dump folder");
        return false;
    }
    Out o;
    o.cs = OpenOut(dir, L"dump.cs");
    o.methods = OpenOut(dir, L"methods.tsv");
    o.fields = OpenOut(dir, L"fields.tsv");
    if (!o.cs || !o.methods || !o.fields) {
        Log("dump: cannot open the output files");
        return false;
    }
    std::fputs("image\tnamespace\tclass\tmethod\tkind\tparams\treturn\trva\n", o.methods);
    std::fputs("image\tnamespace\tclass\tfield\ttype\tkind\toffset\tvalue\n", o.fields);

    const DWORD started = GetTickCount();
    size_t count = 0;
    const Assembly** assemblies = a.domain_get_assemblies(domain, &count);
    FILE* summary = OpenOut(dir, L"summary.txt");
    for (size_t i = 0; i < count; ++i) {
        const Image* img = a.assembly_get_image(assemblies[i]);
        const char* name = Safe(a.image_get_name(img));
        const size_t n = a.image_get_class_count(img);
        const size_t before = o.classes, faultedBefore = o.faulted;
        for (size_t c = 0; c < n; ++c) {
            const Class* k = a.image_get_class(img, c);
            if (k && !DumpClassGuarded(o, name, k)) ++o.faulted;
        }
        if (summary)
            std::fprintf(summary, "%-48s classes %6zu (%zu faulted)\n", name, o.classes - before, o.faulted - faultedBefore);
    }
    const DWORD ms = GetTickCount() - started;
    if (summary) {
        std::fprintf(summary, "\nassemblies %zu, classes %zu, methods %zu, fields %zu, faulted %zu, %lu ms\n", count, o.classes,
                     o.methodsN, o.fieldsN, o.faulted, ms);
        std::fclose(summary);
    }
    std::fclose(o.cs);
    std::fclose(o.methods);
    std::fclose(o.fields);
    Log("dump: %zu assemblies, %zu classes, %zu methods, %zu fields, %zu faulted, %lu ms", count, o.classes, o.methodsN,
        o.fieldsN, o.faulted, ms);
    if (FILE* done = OpenOut(dir, L"done.txt")) {
        std::fprintf(done, "classes %zu\nmethods %zu\nfields %zu\nfaulted %zu\nms %lu\n", o.classes, o.methodsN, o.fieldsN,
                     o.faulted, ms);
        std::fclose(done);
    }
    return true;
}

}  // namespace ep

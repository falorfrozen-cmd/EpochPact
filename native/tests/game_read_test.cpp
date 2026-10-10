// Real production accessors: a failed IL2CPP read must not look like null/dead.
#include "../core/game.cpp"
#include <stdexcept>
#include <cstdio>

namespace fixture {
ep::il2cpp::Api api;
int field, object;
bool fault = false, live = true;
void Read(const ep::il2cpp::Field*, void* out) {
    *static_cast<void**>(out) = &object;
    if (fault) throw std::runtime_error("read failed after partial output");
}
bool Alive(void*, const ep::il2cpp::Method*) {
    if (fault) throw std::runtime_error("lifetime read failed");
    return live;
}
int total = 0, passed = 0;
void Check(bool ok, const char* what) {
    ++total; if (ok) ++passed;
    std::printf("%s %s\n", ok ? "ok" : "FAIL", what);
}
}
namespace ep {
void Log(const char*, ...) {}
namespace il2cpp { const Api& api() { return fixture::api; } }
}
int main() {
    using namespace fixture;
    void* value = &object;
    Check(!ep::game::TryStaticObject(nullptr, &value) && !value, "missing field is unknown");
    Check(!ep::game::TryStaticObject(&field, &value) && !value, "missing API is unknown");
    api.field_static_get_value = Read;
    Check(ep::game::TryStaticObject(&field, &value) && value == &object, "reference read succeeds");
    fault = true;
    Check(!ep::game::TryStaticObject(&field, &value) && !value, "partial failed read cannot leak output");
    Check(!ep::game::StaticObject(&field), "legacy read stays null on failure");
    fault = false;
    api.field_static_get_value = [](const ep::il2cpp::Field*, void* out) { *static_cast<void**>(out) = nullptr; };
    Check(ep::game::TryStaticObject(&field, &value) && !value, "successful null is known");
    bool alive = true;
    Check(ep::game::TryIsAlive(nullptr, &alive) && !alive, "null Unity reference is known dead");
    Check(!ep::game::TryIsAlive(&object, &alive), "missing lifetime method is unknown");
    ep::game::g_opImplicit = {&field, reinterpret_cast<void*>(&Alive)};
    Check(ep::game::TryIsAlive(&object, &alive) && alive, "live Unity object is known");
    live = false;
    Check(ep::game::TryIsAlive(&object, &alive) && !alive, "destroyed Unity object is known");
    fault = true;
    Check(!ep::game::TryIsAlive(&object, &alive) && !alive, "failed lifetime read is unknown");
    Check(!ep::game::IsAlive(&object), "legacy lifetime gate fails closed");
    std::printf("game_read_test: %d/%d passed\n", passed, total);
    return total - passed;
}

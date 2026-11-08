#pragma once
// The hideout's ground, pure rules: its outline as a distance, its fall into rock, its texture without seams.
#include <algorithm>
#include <cmath>
#include <array>

namespace ep::hideout::rules {
enum Shape { kRound = 0, kSquare = 1, kHall = 2, kCross = 3, kIslands = 4, kShapes = 5 };
constexpr float kBand = 10.f;       // how far outside the outline the rock goes on
constexpr float kMinSize = 24.f, kMaxSize = 80.f;
constexpr int kMaxItems = 400, kPreviewCache = 32, kSearchPage = 12;
constexpr int PrimaryMouse(bool swapped){return swapped?0x02:0x01;}
constexpr int CancelMouse(bool swapped){return swapped?0x01:0x02;}
inline constexpr std::array<int,4> kRotationKeys{'Q','E',0x25,0x27}; // Q/E and Left/Right aliases
struct EditorRotation {
    unsigned long long next=0;
    int previous=0;
    float Step(unsigned held,unsigned tapped,unsigned long long now){
        const auto keys=held|tapped;
        const int direction=static_cast<int>((keys&0xA)!=0)-static_cast<int>((keys&0x5)!=0);
        if(!direction){previous=0;return 0;}
        const bool fresh=(tapped&(direction<0?0x5:0xA))!=0;
        const bool turn=direction!=previous||fresh||now>=next;
        previous=direction;
        if(!turn)return 0;
        next=now+110;return direction*15.f;
    }
};
struct KeyLatch {
    bool down=false;
    bool Pressed(bool held,bool recent){const bool edge=(held&&!down)||recent;down=held;return edge;}
};
struct EditorInputGate {
    bool armed=false;
    bool Allow(bool active,bool held){
        if(!active){armed=false;return false;}
        if(!armed){armed=!held;return false;} // drain transitions from another window/menu
        return true;
    }
};
constexpr bool PreemptPreview(bool priority,bool same,unsigned long long progressAge){return priority&&!same&&progressAge>=250;}
struct Rotation {float x,y,z,w;};
inline Rotation YawOnBase(float degrees,Rotation b) {
    const float angle=degrees*3.14159265358979323846f/360, s=std::sin(angle),c=std::cos(angle);
    return {c*b.x+s*b.z,c*b.y+s*b.w,c*b.z-s*b.x,c*b.w-s*b.y};
}
inline bool GroundRay(const std::array<float,3>& o,const std::array<float,3>& d,float y,float& x,float& z) {
    if(!std::isfinite(d[1]) || std::abs(d[1])<.000001f)return false;
    const float t=(y-o[1])/d[1];if(!std::isfinite(t)||t<0)return false;
    x=o[0]+t*d[0];z=o[2]+t*d[2];return std::isfinite(x)&&std::isfinite(z);
}
inline float RayBounds(const std::array<float,3>& o,const std::array<float,3>& d,const std::array<float,3>& c,const std::array<float,3>& e) {
    float low=0,high=1e9f;
    for(int k=0;k<3;++k){if(std::abs(d[k])<.000001f){if(o[k]<c[k]-e[k]||o[k]>c[k]+e[k])return -1;continue;}
        float a=(c[k]-e[k]-o[k])/d[k],b=(c[k]+e[k]-o[k])/d[k];if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);if(high<low)return -1;}
    return low;
}
inline bool FinitePlacement(float x, float z, float lift, float yaw, float scale) {
    return std::isfinite(x) && std::isfinite(z) && std::isfinite(lift) && std::isfinite(yaw) && std::isfinite(scale)
        && std::abs(x) <= 200 && std::abs(z) <= 200 && std::abs(lift) <= 40 && scale >= .001f && scale <= 100;
}
inline float Box(float x, float z, float halfX, float halfZ, float corner) {
    const float qx = std::abs(x) - halfX + corner, qz = std::abs(z) - halfZ + corner;
    const float ox = std::max(qx, 0.f), oz = std::max(qz, 0.f);
    return corner - std::sqrt(ox * ox + oz * oz) - std::min(std::max(qx, qz), 0.f);
}
inline float Circle(float x, float z, float r) { return r - std::sqrt(x * x + z * z); }
// Inside is positive: how far from the edge of the plain outline.
inline float Outline(int shape, float size, float x, float z) {
    switch (shape) {
        case kSquare: return Box(x, z, size * .9f, size * .9f, size * .12f);
        case kHall: return Box(x, z, size * 1.45f, size * .62f, size * .12f);
        case kCross: return std::max(Box(x, z, size * 1.1f, size * .42f, 3.f), Box(x, z, size * .42f, size * 1.1f, 3.f));
        case kIslands: {
            const float big = Circle(x, z, size * .72f);
            const float left = Circle(x + size * 1.35f, z, size * .48f), right = Circle(x - size * 1.35f, z, size * .48f);
            const float path = Box(x, z, size * 1.2f, 3.f, 1.f);
            return std::max({big, left, right, path});
        }
        default: return Circle(x, z, size);
    }
}
// The same, with an uneven edge, so nothing ends in a ruled line. The middle of the ground stays where it is.
inline float Distance(int shape, float size, float x, float z) {
    return Outline(shape, size, x, z) + .7f * std::sin(x * .31f + z * .17f) + .5f * std::sin(z * .43f - x * .23f) + .3f * std::sin(x * .91f + z * .77f);
}
inline bool Inside(int shape, float size, float x, float z, float radius = 0) {
    return std::isfinite(x) && std::isfinite(z) && Outline(shape, size, x, z) >= radius + 1.5f;
}
// Flat on top; outside the edge the rock falls away fast.
inline float Height(float distance, float rock) {
    if (distance >= 0) return 0;
    const float t = std::min(-distance / kBand, 1.f);
    return -rock * std::pow(t, .55f);
}
// A texture that does not tile is mirrored at each repeat instead: no cut anywhere. Vertices must lie on the folds.
inline float Mirrored(float position, float period) {
    const float v = std::abs(position) / period;
    const float within = v - 2.f * std::floor(v / 2.f);
    return within <= 1.f ? within : 2.f - within;
}
// Where a grid point lies on the ground. The texture reads straight along the grid, and mirrors on grid lines: that leaves no cut and no broken
// triangle. For a natural texture the grid itself leans and bends and misses the middle, so no pattern points at its lines. A laid floor stays
// straight: its mirror is part of the design.
struct Point { float x, z; };
inline Point GridPoint(float a, float b, bool natural) {
    if (!natural) return {a, b};
    const float c = .9063f, s = .4226f;  // 25 degrees
    const float x = a * c - b * s, z = a * s + b * c;
    return {x + 5.f * std::sin(z * .09f + 1.3f) + 1.8f * std::sin(z * .23f) + 7.3f, z + 5.f * std::sin(x * .09f + .4f) + 1.8f * std::sin(x * .23f + 2.1f) + 11.1f};
}
// The part of a cell inside the outline, as a polygon: corners in order round the cell, d their distances (inside is positive).
// A corner stays if inside; an edge that crosses the outline gets a point where the distance runs out. Returns how many points.
inline int ClipCell(const Point corner[4], const float d[4], Point out[8]) {
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        if (d[i] >= 0) out[n++] = corner[i];
        if ((d[i] >= 0) != (d[j] >= 0)) {
            const float t = d[i] / (d[i] - d[j]);
            out[n++] = {corner[i].x + (corner[j].x - corner[i].x) * t, corner[i].z + (corner[j].z - corner[i].z) * t};
        }
    }
    return n;
}
// Ready templates lay the ten boss doors out: two rows, a ring, or two rows along a long hall.
enum Layout { kRows = 0, kRing = 1, kLongRows = 2, kArms = 3, kInnerRing = 4 };
inline Point BossDoor(int layout, int index, float size) {
    switch (layout) {
        case kRing:
        case kInnerRing: {
            const float a = (index + .5f) * 6.2831853f / 10.f, r = size * (layout == kRing ? .68f : .5f);
            return {std::cos(a) * r, std::sin(a) * r};
        }
        case kLongRows: return {(index % 5 - 2) * size * .48f, index < 5 ? size * .36f : -size * .36f};
        case kArms: {
            // along the arms of a cross: two up, three right... in turn
            const float reach = size * (.42f + .2f * (index / 4));
            const int arm = index % 4;
            return arm == 0 ? Point{reach, 0} : arm == 1 ? Point{0, reach} : arm == 2 ? Point{-reach, 0} : Point{0, -reach};
        }
        default: return {(index % 5 - 2) * size * .29f, index < 5 ? size * .42f : -size * .32f};
    }
}
inline bool ValidName(const char* name) {
    int n = 0;
    for (const char* c = name; *c; ++c, ++n) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' || *c == '_';
        if (!ok || n >= 40) return false;
    }
    return n > 0 && name[0] != ' ' && name[n - 1] != ' ';
}
inline float MaxExtent(int shape, float size) {
    switch (shape) {
        case kHall: return size * 1.45f;
        case kCross: return size * 1.1f;
        case kIslands: return size * 1.83f;
        case kSquare: return size * .9f * 1.42f;
        default: return size;
    }
}
}  // namespace ep::hideout::rules

#pragma once
namespace ep::items::rules {
// The native pickup routine checks the record's location. Borrow it for this
// synchronous call only, restoring even when inventory/managed code rejects it.
// The pickup scan looks at no more than `cap` list entries per tick. A longer
// list (hundreds of labels after a big drop) is walked in turns: each scan
// starts where the previous one stopped, so every entry gets a turn and one
// stuck group at the front never hides the rest.
struct ScanWindow { int start = 0, count = 0; };
inline ScanWindow NextWindow(int size, int cap, unsigned long long& cursor) {
    if (size <= 0 || cap <= 0) { cursor = 0; return {}; }
    if (size <= cap) { cursor = 0; return {0, size}; }
    const int start = static_cast<int>(cursor % static_cast<unsigned long long>(size));
    cursor = static_cast<unsigned long long>((start + cap) % size);
    return {start, cap};
}
template<class T> struct BorrowLocation {
    T& field;T before;
    BorrowLocation(T& target,const T& temporary):field(target),before(target){field=temporary;}
    ~BorrowLocation(){field=before;}
    BorrowLocation(const BorrowLocation&)=delete;
};
}

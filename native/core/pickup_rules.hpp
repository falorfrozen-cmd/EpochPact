#pragma once
namespace ep::items::rules {
// The native pickup routine checks the record's location. Borrow it for this
// synchronous call only, restoring even when inventory/managed code rejects it.
template<class T> struct BorrowLocation {
    T& field;T before;
    BorrowLocation(T& target,const T& temporary):field(target),before(target){field=temporary;}
    ~BorrowLocation(){field=before;}
    BorrowLocation(const BorrowLocation&)=delete;
};
}

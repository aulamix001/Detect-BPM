#pragma once
#include <cstdlib>
#include <new>
#include <cstddef>
namespace test_rt {
inline thread_local bool checking=false;
inline thread_local unsigned operations=0;
struct Guard {
    Guard(){operations=0;checking=true;}
    unsigned finish(){checking=false;return operations;}
    ~Guard(){checking=false;}
};
}
void* operator new(std::size_t n) {
    if(test_rt::checking)++test_rt::operations;
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(test_rt::checking&&p)++test_rt::operations;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

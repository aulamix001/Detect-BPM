#include "RealtimeAnalyser.h"
#include "AllocationGuard.h"
#include <iostream>
#include <thread>
int main() {
    chi::SpscQueue<unsigned,17> queue;
    unsigned value=999;
    if(queue.pop(value))return 1;
    for(unsigned i=0;i<16;++i)if(!queue.push(i))return 2;
    if(queue.push(17))return 3;
    for(unsigned i=0;i<16;++i)if(!queue.pop(value)||value!=i)return 4;
    std::atomic<bool> failed{false};
    std::thread consumer([&]{test_rt::Guard guard;for(unsigned i=0;i<200000;++i){while(!queue.pop(value))std::this_thread::yield();if(value!=i)failed=true;}if(guard.finish())failed=true;});
    test_rt::Guard guard;
    for(unsigned i=0;i<200000;++i)while(!queue.push(i))std::this_thread::yield();
    const auto allocations=guard.finish();consumer.join();
    if(failed||allocations)return 5;
    std::cout<<"PASS 200000 ordered concurrent transfers, wrap/full/empty, zero new/delete\n";return 0;
}

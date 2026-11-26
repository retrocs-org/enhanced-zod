#ifndef ZMP_ASYNC_H
#define ZMP_ASYNC_H
#include <atomic>
#include <memory>
#include <thread>

// Workers own their results, never menu widgets. Closing a menu does not wait
// for network I/O, and its outstanding result is safely discarded.
template<class T> class ZMPAsync
{
    struct State { T value{}; std::atomic<bool> done{false}; };
    std::shared_ptr<State> state;
public:
    void Cancel(){state.reset();}
    bool Pending() const { return bool(state); }
    template<class F> void Start(F work) {
        if(state) return;
        state = std::make_shared<State>();
        auto result = state;
        std::thread([result, work]() {
            try { result->value = work(); } catch(...) { result->value = T{}; }
            result->done.store(true);
        }).detach();
    }
    bool Poll(T &out) {
        if(!state || !state->done.load()) return false;
        out = std::move(state->value); state.reset(); return true;
    }
};
#endif

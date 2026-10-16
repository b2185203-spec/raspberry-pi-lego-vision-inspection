#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <utility>
using MeasureClock=std::chrono::steady_clock;
struct MeasureCell { double ms=0; int calls=0; };
inline std::map<std::string,MeasureCell> board_measurements;
inline std::function<void(const std::string&)> memory_checkpoint_hook;
inline void memory_checkpoint(const std::string& name) {
    if(memory_checkpoint_hook) memory_checkpoint_hook(name);
}
struct MeasureScope {
    const char* name;
    MeasureClock::time_point start;
    explicit MeasureScope(const char* n):name(n),start(MeasureClock::now()){}
    ~MeasureScope(){
        const auto end=MeasureClock::now();
        auto& item=board_measurements[name];
        item.ms+=std::chrono::duration<double,std::milli>(end-start).count(); ++item.calls;
    }
};
template<class F> auto measured_call(const char* name,F&& f) {
    MeasureScope timer(name); return std::forward<F>(f)();
}

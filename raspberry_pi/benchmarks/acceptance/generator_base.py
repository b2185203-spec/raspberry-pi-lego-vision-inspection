"""Add measurement hooks to an exact copy; leave production sources untouched."""
import hashlib
import pathlib
import sys

source = pathlib.Path(sys.argv[1])
target = pathlib.Path(sys.argv[2])
text = source.read_text(encoding='utf-8')
def replace(old, new):
    global text
    if text.count(old) != 1:
        raise RuntimeError('Source changed: expected exactly one instrumentation anchor: '+old)
    text = text.replace(old, new, 1)

replace('#pragma once', '''#pragma once
#include <chrono>
#include <stdexcept>
struct BenchmarkStageTiming {
    double preprocess_ms=0, run_ms=0, postprocess_ms=0, total_ms=0, nms_ms=0;
};
struct BenchmarkScope {
    using Clock=std::chrono::steady_clock;
    BenchmarkStageTiming& result;
    Clock::time_point start=Clock::now(), run_start, run_end;
    explicit BenchmarkScope(BenchmarkStageTiming& r): result(r) { result={}; }
    ~BenchmarkScope() {
        const auto end=Clock::now();
        result.preprocess_ms=std::chrono::duration<double,std::milli>(run_start-start).count();
        result.run_ms=std::chrono::duration<double,std::milli>(run_end-run_start).count();
        result.postprocess_ms=std::chrono::duration<double,std::milli>(end-run_end).count();
        result.total_ms=std::chrono::duration<double,std::milli>(end-start).count();
    }
};''')
replace('explicit YoloDetector(const std::string& model_path)', 'explicit YoloDetector(const std::string& model_path, const std::string& profile_prefix = "")')
replace('        session_ = std::make_unique<Ort::Session>(', '''        if (!profile_prefix.empty()) session_options_.EnableProfiling(profile_prefix.c_str());
        session_ = std::make_unique<Ort::Session>(''')
replace('        input_name_ = input_name.get();', '''        input_name_ = input_name.get();
        const auto input_shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        const auto output_shape = session_->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (input_shape != std::vector<int64_t>{1,3,640,640} || output_shape != std::vector<int64_t>{1,9,8400})
            throw std::runtime_error("Unexpected model shape; original decoder requires [1,9,8400]");''')
replace('        detections.clear();', '        BenchmarkScope benchmark_scope(last_timing);\n        detections.clear();')
replace('        auto output_tensors = session_->Run(', '        benchmark_scope.run_start = BenchmarkScope::Clock::now();\n        auto output_tensors = session_->Run(')
replace('        const float* output = output_tensors[0].GetTensorData<float>();', '''        benchmark_scope.run_end = BenchmarkScope::Clock::now();
        const float* output = output_tensors[0].GetTensorData<float>();''')
replace('        cv::dnn::NMSBoxes(', '        const auto nms_start = BenchmarkScope::Clock::now();\n        cv::dnn::NMSBoxes(')
replace('        if (kept_indices.empty())', '''        last_timing.nms_ms = std::chrono::duration<double,std::milli>(BenchmarkScope::Clock::now()-nms_start).count();
        if (kept_indices.empty())''')
replace('private:\n    Ort::Env environment_;', '''public:
    BenchmarkStageTiming last_timing;
    std::string finishProfiling() {
        Ort::AllocatorWithDefaultOptions allocator;
        auto filename=session_->EndProfilingAllocated(allocator);
        return filename.get();
    }
private:
    Ort::Env environment_;''')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(text, encoding='utf-8')
print('Instrumented original SHA256:', hashlib.sha256(source.read_bytes()).hexdigest())

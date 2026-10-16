#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include "image_preprocessor_measured.hpp"
#include "yolo_detector_measured.hpp"
#include "position_rules_measured.hpp"
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
static long long monotonic_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();}
static std::string utc_time(){
    const auto now=std::chrono::system_clock::now(); const auto value=std::chrono::system_clock::to_time_t(now);
    std::tm tm{}; gmtime_r(&value,&tm); std::ostringstream s;
    s<<std::put_time(&tm,"%Y-%m-%dT%H:%M:%S")<<'.'<<std::setw(6)<<std::setfill('0')
     <<std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000<<'Z'; return s.str();
}
static std::map<std::string,long long> proc_fields(const std::string& file){
    std::ifstream in(file); std::map<std::string,long long> out; std::string line;
    while(std::getline(in,line)){auto colon=line.find(':'); if(colon==std::string::npos)continue;
        std::istringstream value(line.substr(colon+1)); long long n; if(value>>n)out[line.substr(0,colon)]=n;}
    return out;
}
struct MemoryRow{std::string timestamp,phase; long long clock_ns; int run_index; std::map<std::string,long long> status,pss,system;};
struct TimingRow{
    int run_index,image_index,detections; std::string image; std::array<double,11> values;
    std::map<std::string,MeasureCell> board;
};
int main(int argc,char** argv){
    try{
        if(argc!=6)throw std::runtime_error("Usage: full_benchmark latency|monitor|profile MODEL IMAGES LIST OUTPUT");
        std::string mode=argv[1]; if(mode!="latency"&&mode!="monitor"&&mode!="profile")throw std::runtime_error("Invalid mode");
        fs::path model=fs::absolute(argv[2]),image_dir=argv[3],list=argv[4],out=argv[5];
        if(fs::exists(out/"stage_timing.csv"))throw std::runtime_error("Existing results; choose a fresh mode directory");
        fs::create_directories(out);
        std::vector<MemoryRow> memory; int run_index=-1;
        if(mode=="monitor")memory_checkpoint_hook=[&](const std::string& phase){
            MemoryRow row{utc_time(),phase,monotonic_ns(),run_index,proc_fields("/proc/self/status"),proc_fields("/proc/self/smaps_rollup"),proc_fields("/proc/meminfo")};
            memory.push_back(std::move(row));
        };
        const auto initial_system=proc_fields("/proc/meminfo");
        memory_checkpoint("program_entry");
        ImagePreprocessor preprocessor; PositionRules rules;
        std::vector<std::string> names; std::ifstream input_list(list); std::string name;
        if(!input_list)throw std::runtime_error("Cannot read input list");
        while(std::getline(input_list,name)){while(!name.empty()&&(name.back()=='\r'||name.back()=='\n'))name.pop_back(); if(!name.empty())names.push_back(name);}
        if(names.size()<10||names.size()>20)throw std::runtime_error("Require 10-20 fixed images");
        std::ofstream preflight(out/"input_validation.csv"); preflight<<"image,width,height,board_ok,rectified_width,rectified_height\n";
        for(size_t i=0;i<names.size();++i){
            cv::Mat image=cv::imread((image_dir/names[i]).string()),rectified; std::string message;
            if(image.empty())throw std::runtime_error("Cannot decode "+names[i]);
            bool valid=preprocessor.process(image,rectified,message);
            preflight<<names[i]<<','<<image.cols<<','<<image.rows<<','<<valid<<','<<rectified.cols<<','<<rectified.rows<<'\n';
            if(!valid)throw std::runtime_error("Original board detection failed: "+names[i]);
            if(i==0){cv::imwrite((out/"first_original_input.jpg").string(),image);cv::imwrite((out/"first_rectified_input.jpg").string(),rectified);}
        }
        preflight.close(); board_measurements.clear(); memory_checkpoint("before_detector_model_load");
        const auto init_start=Clock::now();
        auto detector=std::make_unique<YoloDetector>(model.string(),mode=="profile"?(out/"ort_profile").string():"");
        const double initialization_ms=elapsed(init_start,Clock::now());
        memory_checkpoint("after_detector_model_load"); memory_checkpoint("warmup_before");
        std::vector<TimingRow> rows; rows.reserve(120);
        std::ofstream detection_csv(out/"detections.csv");
        detection_csv<<"run_index,phase,measured_iteration,image,class_id,score,x,y,width,height\n";
        for(int i=0;i<120;++i){
            run_index=i; const int image_index=i%names.size();
            if(i==20){memory_checkpoint("warmup_after");memory_checkpoint("measurement_before");}
            board_measurements.clear(); cv::Mat image,rectified,preview; std::string board_message;
            std::vector<DetectedObject> detections;
            memory_checkpoint("iteration_before"); const auto start=Clock::now();
            image=cv::imread((image_dir/names[image_index]).string()); const auto read_end=Clock::now();
            if(image.empty())throw std::runtime_error("Decode failed during run");
            bool valid=preprocessor.process(image,rectified,board_message); const auto board_end=Clock::now();
            if(!valid)throw std::runtime_error("Board detection failed during run");
            std::string yolo_message=detector->detect(rectified,preview,detections); const auto detect_end=Clock::now();
            std::string position_message=rules.check(preview,detections);
            std::string business_result=yolo_message+"\n"+position_message;
            const auto end=Clock::now(); memory_checkpoint("iteration_after");
            const auto t=detector->last_timing;
            rows.push_back({i,image_index,static_cast<int>(detections.size()),names[image_index],
                {elapsed(start,read_end),elapsed(read_end,board_end),t.preprocess_ms,t.run_ms,t.postprocess_ms,
                 elapsed(detect_end,end),elapsed(start,end),t.nms_ms,t.total_ms,elapsed(board_end,detect_end),
                 elapsed(start,end)-elapsed(start,read_end)-elapsed(read_end,board_end)-t.preprocess_ms-t.run_ms-t.postprocess_ms-elapsed(detect_end,end)},board_measurements});
            for(const auto& d:detections)detection_csv<<i<<','<<(i<20?"warmup":"measured")<<','<<(i<20?-1:i-20)<<','<<names[image_index]<<','<<d.class_id<<','<<std::setprecision(9)<<d.score<<','<<d.box.x<<','<<d.box.y<<','<<d.box.width<<','<<d.box.height<<'\n';
            if(preview.empty()||business_result.empty())throw std::runtime_error("Invalid pipeline output");
            if(i==119){cv::imwrite((out/"last_detection_and_business_preview.jpg").string(),preview);std::ofstream(out/"last_business_result.txt")<<business_result;}
        }
        memory_checkpoint("measurement_after");
        if(mode=="profile"){
            auto file=detector->finishProfiling(); fs::copy_file(file,out/"ort_profile.json",fs::copy_options::none);
            std::ofstream(out/"profile_original_filename.txt")<<file<<'\n';
        }
        const char* fields[]={"image_read_ms","board_detection_correction_ms","yolo_preprocess_ms","ort_run_ms","postprocess_ms","business_position_rules_ms","image_pipeline_total_ms","nms_ms","detector_total_ms","detector_call_ms","unattributed_overhead_ms"};
        for(bool warm:{false,true}){
            std::ofstream csv(out/(warm?"warmup_timing.csv":"stage_timing.csv"));
            csv<<std::fixed<<std::setprecision(9)<<"run_index,iteration,image_index,image,detections";
            for(auto field:fields){csv<<','<<field;} csv<<'\n';
            for(const auto& row:rows){if((row.run_index<20)!=warm)continue;
                csv<<row.run_index<<','<<(warm?row.run_index:row.run_index-20)<<','<<row.image_index<<','<<row.image<<','<<row.detections;
                for(double value:row.values){csv<<','<<value;} csv<<'\n';}
        }
        std::ofstream board(out/"board_step_timing.csv");board<<std::fixed<<std::setprecision(9)<<"run_index,phase,iteration,image,stage,call_count,duration_ms\n";
        for(const auto& row:rows)for(const auto& pair:row.board)board<<row.run_index<<','<<(row.run_index<20?"warmup":"measured")<<','<<(row.run_index<20?-1:row.run_index-20)<<','<<row.image<<','<<pair.first<<','<<pair.second.calls<<','<<pair.second.ms<<'\n';
        std::ofstream config(out/"config.txt");config<<"mode="<<mode<<"\nmodel="<<model<<"\nORT="<<Ort::GetVersionString()<<"\nOpenCV="<<CV_VERSION<<"\ninput=float32[1,3,640,640]\noutput=float32[1,9,8400]\nimages="<<names.size()<<"\nwarmup=20\nmeasured=100\nmodel_load_and_session_init_ms="<<initialization_ms<<"\nORT_intra_op_threads=2\nORT_graph_optimization=ORT_ENABLE_EXTENDED\nORT_EP=CPUExecutionProvider\nORT_inter_op_threads=default_unchanged\nORT_execution_mode=default_sequential\ncv_threads="<<cv::getNumThreads()<<"\ninitial_system_MemTotal_kib="<<initial_system.at("MemTotal")<<"\ninitial_system_MemAvailable_kib="<<initial_system.at("MemAvailable")<<"\n";
        std::ofstream(out/"model_load_session_timing.csv")<<"stage,duration_ms,note\nmodel_load_and_session_creation,"<<std::setprecision(12)<<initialization_ms<<",ORT Session constructor loads model; model loading and Session creation not independently separable\n";
        detection_csv.close(); board.close();config.close();memory_checkpoint("before_session_destroy");detector.reset();memory_checkpoint("after_session_destroy");memory_checkpoint("process_before_exit");
        if(mode=="monitor"){
            std::ofstream csv(out/"memory_checkpoints.csv");csv<<"timestamp_utc,monotonic_ns,pid,run_index,phase,VmRSS_kib,VmHWM_kib,VmSize_kib,VmPeak_kib,Pss_kib,Private_Clean_kib,Private_Dirty_kib,MemAvailable_kib\n";
            for(const auto& r:memory){csv<<r.timestamp<<','<<r.clock_ns<<','<<getpid()<<','<<r.run_index<<','<<r.phase;
                for(const auto* key:{"VmRSS","VmHWM","VmSize","VmPeak"}){csv<<',';if(r.status.count(key))csv<<r.status.at(key);}
                for(const auto* key:{"Pss","Private_Clean","Private_Dirty"}){csv<<',';if(r.pss.count(key))csv<<r.pss.at(key);}
                csv<<',';if(r.system.count("MemAvailable"))csv<<r.system.at("MemAvailable");csv<<'\n';}
        }
        memory_checkpoint_hook={};
        std::cout<<"SUCCESS mode="<<mode<<" images="<<names.size()<<" warmup=20 measured=100 init_ms="<<initialization_ms<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

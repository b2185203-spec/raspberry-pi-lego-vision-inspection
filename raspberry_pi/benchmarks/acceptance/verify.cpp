#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef ACCEPTANCE_INSTRUMENTED
#include "image_preprocessor_measured.hpp"
#include "yolo_detector_measured.hpp"
#include "position_rules_measured.hpp"
#else
#include "image_preprocessor.hpp"
#include "yolo_detector.hpp"
#include "position_rules.hpp"
#endif
namespace fs=std::filesystem;
static void bytes(const fs::path& p,const void* data,size_t n){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(data),n);if(!f)throw std::runtime_error("Write failed");}
static void image(const fs::path& p,const cv::Mat& m){if(!m.isContinuous())throw std::runtime_error("Noncontiguous verification output");bytes(p,m.data,m.total()*m.elemSize());}
int main(int argc,char** argv){
    try {
        if(argc!=5)throw std::runtime_error("Usage: acceptance_verify MODEL IMAGES LIST NEW_OUTPUT");
        const fs::path out=argv[4];if(fs::exists(out))throw std::runtime_error("Choose a fresh output directory");fs::create_directories(out);
        std::vector<std::string> names;std::ifstream list(argv[3]);std::string name;
        while(std::getline(list,name)){if(!name.empty()&&name.back()=='\r')name.pop_back();if(!name.empty())names.push_back(name);}
        if(names.size()!=15)throw std::runtime_error("Expected original 15 images");
#ifdef ACCEPTANCE_INSTRUMENTED
        acceptance_capture_enabled=true;
#endif
        ImagePreprocessor preprocessor;YoloDetector detector(argv[1]);PositionRules rules;
        std::ofstream det(out/"detections.csv"),board(out/"board_corners.csv"),metadata(out/"image_results.csv");
        det<<"image,class_id,score,x,y,width,height,center_x,center_y\n"<<std::setprecision(9);
        board<<"image,corner,x,y\n"<<std::setprecision(9);
        metadata<<"image,board_ok,rectified_width,rectified_height,detections\n";
        for(size_t i=0;i<names.size();++i){
            const auto dir=out/('i'+std::to_string(i));fs::create_directory(dir);
            cv::Mat original=cv::imread((fs::path(argv[2])/names[i]).string()),rectified,preview;std::string board_message;
            if(original.empty()||!preprocessor.process(original,rectified,board_message))throw std::runtime_error("Board detection failed: "+names[i]);
            std::vector<DetectedObject> objects;const std::string yolo=detector.detect(rectified,preview,objects);const std::string business=rules.check(preview,objects);
            image(dir/"rectified.bgr",rectified);image(dir/"preview.bgr",preview);
            bytes(dir/"board_message.txt",board_message.data(),board_message.size());bytes(dir/"yolo_message.txt",yolo.data(),yolo.size());bytes(dir/"business_judgment.txt",business.data(),business.size());
            for(const auto& d:objects)det<<names[i]<<','<<d.class_id<<','<<d.score<<','<<d.box.x<<','<<d.box.y<<','<<d.box.width<<','<<d.box.height<<','<<d.center.x<<','<<d.center.y<<'\n';
            metadata<<names[i]<<",1,"<<rectified.cols<<','<<rectified.rows<<','<<objects.size()<<'\n';
#ifdef ACCEPTANCE_INSTRUMENTED
            image(dir/"mask.u8",acceptance_mask);bytes(dir/"tensor.f32",acceptance_tensor.data(),acceptance_tensor.size()*sizeof(float));
            for(int k=0;k<4;++k)board<<names[i]<<','<<k<<','<<acceptance_corners[k].x<<','<<acceptance_corners[k].y<<'\n';
#endif
        }
        std::ofstream(out/"verification_complete.txt")<<"images=15\nSessionOptions=production_source_verbatim\n";
        std::cout<<"PASS images=15\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

"""Instrument copies of the supplied production source. Never override SessionOptions."""
import json
import pathlib
import subprocess
import sys
source=pathlib.Path(sys.argv[1]);target=pathlib.Path(sys.argv[2]);here=pathlib.Path(__file__).parent
subprocess.run([sys.executable,str(here/'base_generate.py'),str(source),str(target)],check=True)
(target/'measurement_support.hpp').write_text((here/'measurement_support_base.hpp').read_text(encoding='utf-8')+'\n#include "capture.hpp"\n',encoding='utf-8')
def replace(text,a,b):
    assert text.count(a)==1,a
    return text.replace(a,b)
p=target/'image_preprocessor_measured.hpp';s=p.read_text(encoding='utf-8')
s=replace(s,'        rectified = measured_call(', '        if(acceptance_capture_enabled) acceptance_corners=corners;\n        rectified = measured_call(')
s=replace(s,'        return mask;', '        if(acceptance_capture_enabled) acceptance_mask=mask.clone();\n        return mask;')
p.write_text(s,encoding='utf-8')
p=target/'yolo_detector_measured.hpp';s=p.read_text(encoding='utf-8')
s=replace(s,'        const float* output = output_tensors[0].GetTensorData<float>();',
    '        const float* output = output_tensors[0].GetTensorData<float>();\n        if(acceptance_capture_enabled) acceptance_tensor.assign(output,output+output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount());')
p.write_text(s,encoding='utf-8')
original=(source/'yolo_detector.hpp').read_text(encoding='utf-8')
threads=2 if 'session_options_.SetIntraOpNumThreads(2);' in original else 0
assert original.count('SetIntraOpNumThreads(')==(1 if threads else 0)
assert 'ORT_ENABLE_EXTENDED' in original and 'SetExecutionMode' not in original and 'allow_spinning' not in original
text=(here/'benchmark_base.cpp').read_text(encoding='utf-8')
text=replace(text,'ORT_intra_op_threads=2\\n','ORT_intra_op_threads='+str(threads)+'\\n')
text=replace(text,'        std::ofstream detection_csv(out/"detections.csv");',
    '        std::ofstream business_csv(out/"business_results.csv");business_csv<<"run_index,image,board_message_hex,yolo_message_hex,rules_message_hex\\n";\n        std::ofstream detection_csv(out/"detections.csv");')
text=replace(text,'            const auto t=detector->last_timing;',
    '            business_csv<<i<<\',\'<<names[image_index]<<\',\'<<hex_string(board_message)<<\',\'<<hex_string(yolo_message)<<\',\'<<hex_string(position_message)<<\'\\n\';\n            const auto t=detector->last_timing;')
text=replace(text,'namespace fs=std::filesystem;',
    'static std::string hex_string(const std::string& s){const char* h="0123456789abcdef";std::string r;for(unsigned char c:s){r+=h[c>>4];r+=h[c&15];}return r;}\nnamespace fs=std::filesystem;')
text=text.replace('mode!="latency"&&mode!="monitor"&&mode!="profile"','mode!="latency"&&mode!="monitor"')
(target/'acceptance_benchmark.cpp').write_text(text,encoding='utf-8')
manifest=json.loads((target/'instrumentation_manifest.json').read_text(encoding='utf-8'))
manifest.update(actual_intra_op_threads=threads,session_options='Taken verbatim from production source; no runtime or generated override',
    captures='Verification executable only: exact board mask, selected corners, rectified image, raw output tensor, detections and all business messages')
(target/'instrumentation_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')

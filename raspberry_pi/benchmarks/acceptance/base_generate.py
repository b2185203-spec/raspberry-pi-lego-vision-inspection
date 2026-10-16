"""Instrument copies, preserving all production algorithm statements and settings."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

root=pathlib.Path(sys.argv[1]); target=pathlib.Path(sys.argv[2]); target.mkdir(parents=True,exist_ok=True)
here=pathlib.Path(__file__).resolve().parent
subprocess.run([sys.executable,str(here/'generator_base.py'),str(root/'yolo_detector.hpp'),str(target/'yolo_detector_measured.hpp')],check=True)
p=target/'yolo_detector_measured.hpp'; text=p.read_text(encoding='utf-8')
def single(old,new):
    global text
    if text.count(old)!=1: raise RuntimeError('Ambiguous detector anchor: '+old)
    text=text.replace(old,new)
single('#pragma once','#pragma once\n#include "measurement_support.hpp"')
single('        session_ = std::make_unique<Ort::Session>(', '        memory_checkpoint("model_load_session_before");\n        session_ = std::make_unique<Ort::Session>(')
anchor='        Ort::AllocatorWithDefaultOptions allocator;'
text=text.replace(anchor,'        memory_checkpoint("model_load_session_after");\n'+anchor,1)
single('        benchmark_scope.run_start = BenchmarkScope::Clock::now();','        memory_checkpoint("ort_run_before");\n        benchmark_scope.run_start = BenchmarkScope::Clock::now();')
single('        benchmark_scope.run_end = BenchmarkScope::Clock::now();','        benchmark_scope.run_end = BenchmarkScope::Clock::now();\n        memory_checkpoint("ort_run_after");')
p.write_text(text,encoding='utf-8')

text=(root/'image_preprocessor.hpp').read_text(encoding='utf-8')
text=text.replace('#pragma once','#pragma once\n#include "measurement_support.hpp"',1)
counts={}
def wrap_calls(text,function,label,expected):
    positions=[m.start() for m in re.finditer(re.escape(function)+r'\s*\(',text)]
    if len(positions)!=expected: raise RuntimeError(f'Unexpected {function} count: {len(positions)}')
    for start in reversed(positions):
        opening=text.index('(',start); depth=1; end=opening+1
        while depth:
            if text[end]=='(': depth+=1
            elif text[end]==')': depth-=1
            end+=1
        expression=text[start:end]
        actual=label
        if function=='cv::morphologyEx': actual='board_morph_open' if 'MORPH_OPEN' in expression else 'board_morph_close'
        text=text[:start]+f'measured_call("{actual}", [&]() {{ return {expression}; }})'+text[end:]
    counts[function]=expected
    return text
for function,label,count in [
    ('cv::cvtColor','board_hsv_conversion',1),('cv::inRange','board_threshold',1),
    ('cv::getStructuringElement','board_kernel_creation',2),('cv::morphologyEx','board_morphology',2),
    ('cv::findContours','board_find_contours',1),('cv::contourArea','board_contour_area',1),
    ('cv::minAreaRect','board_min_area_rect',2),('cv::arcLength','board_arc_length',1),
    ('cv::approxPolyDP','board_polygon_approximation',1),('cv::isContourConvex','board_convexity_check',1),
    ('cv::getPerspectiveTransform','board_transform_matrix',1),('cv::warpPerspective','board_warp_perspective',1)]:
    text=wrap_calls(text,function,label,count)
for expression,label,count in [
    ('makeBoardMask(image)','board_mask_total',1),('orderCorners(points)','board_corner_ordering',2),
    ('findBoardCorners(original, corners)','board_find_corners_total',1),
    ('rectifyBoard(original, corners)','board_rectify_total',1)]:
    if text.count(expression)!=count: raise RuntimeError('Unexpected expression '+expression)
    text=text.replace(expression,f'measured_call("{label}", [&]() {{ return {expression}; }})')
(target/'image_preprocessor_measured.hpp').write_text(text,encoding='utf-8')
position=(root/'position_rules.hpp').read_text(encoding='utf-8')
if position.count('#include "yolo_detector.hpp"')!=1: raise RuntimeError('PositionRules include changed')
(target/'position_rules_measured.hpp').write_text(position.replace('#include "yolo_detector.hpp"','#include "yolo_detector_measured.hpp"'),encoding='utf-8')
manifest={'production_files':{f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in ['main.cpp','yolo_detector.hpp','image_preprocessor.hpp','position_rules.hpp','CMakeLists.txt']},
          'wrapped_board_calls':counts,'settings':'Unchanged: original SessionOptions, preprocessing, NMS, PositionRules; generated copies only'}
(target/'instrumentation_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('Measurement copies generated; no production files modified')

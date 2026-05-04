#include "HumanSeg.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <array>
#include <opencv2/dnn.hpp>
HumanSeg::HumanSeg(float conf_thres) : conf_threshold(conf_thres) {
    // 初始化均值和标准差
    mean = (cv::Mat_<float>(1, 3) << 0.5, 0.5, 0.5);
    std = (cv::Mat_<float>(1, 3) << 0.5, 0.5, 0.5);
    // 加载ONNX模型
    try {
        const ORTCHAR_T* model_path = L"modnet.onnx";
        Ort::SessionOptions session_options;
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw > 0) {
            session_options.SetIntraOpNumThreads(static_cast<int>(hw));
        }
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        std::vector<std::string> available_providers = Ort::GetAvailableProviders();
        bool use_cuda = false;
        // for (const auto& provider : available_providers) {
        //     if (provider == "CUDAExecutionProvider") {
        //         OrtCUDAProviderOptions cuda_options;
        //         memset(&cuda_options, 0, sizeof(OrtCUDAProviderOptions)); // 初始化默认值
        //         cuda_options.device_id = 0;
        //         session_options.AppendExecutionProvider_CUDA(cuda_options);
        //         use_cuda = true;
        //         break;
        //     }
        // }
        if (!use_cuda) {
            qDebug() << "CPUMODE\n";
        }
        else{
            qDebug() <<"CUDAMODE模式\n";
        }
        ort_session = std::make_unique<Ort::Session>(
            env,
            model_path,
            session_options
            );
    } catch (const Ort::Exception& e) {
        std::cerr << "No ONNX Model!" << e.what() << std::endl;
        throw std::runtime_error("No ONNX Model!");
    } catch (const std::exception& e) {
        std::cerr << "ONNX model wrong!" << e.what() << std::endl;
        throw std::runtime_error("ONNX model wrong!");
    }
}
// 析构函数
HumanSeg::~HumanSeg() {
    release();
}

// 设置背景（支持中文路径）
void HumanSeg::setBackground(const std::string& bg_path, const std::string& bg_type) {
    this->bg_type = bg_type;
    bg_resized_cache.release();
    cached_bg_w = -1;
    cached_bg_h = -1;

    if (bg_type == "image") {
        bg_image = imreadChinese(bg_path);
        if (bg_image.empty()) {
            throw std::runtime_error("bg picture-->" + bg_path + "-->cannot load!");
        }
        if (bg_image.channels() == 4) {
            cv::cvtColor(bg_image, bg_image, cv::COLOR_BGRA2BGR);
        } else if (bg_image.channels() == 1) {
            cv::cvtColor(bg_image, bg_image, cv::COLOR_GRAY2BGR);
        }
    } else if (bg_type == "video") {
        std::string w_bg_path(bg_path.begin(), bg_path.end());
        bg_video.open(w_bg_path);
        if (!bg_video.isOpened()) {
            throw std::runtime_error("bg video: " + bg_path + "-->cannot load!");
        }
    } else {
        throw std::invalid_argument("bg_type must be image or video！");
    }
}

// 获取适配尺寸的背景帧（图片背景命中尺寸缓存，避免每帧 resize）
cv::Mat HumanSeg::getBgFrame(const std::pair<int, int>& target_size) {
    int target_h = target_size.first;
    int target_w = target_size.second;

    if (bg_type == "image") {
        if (target_w != cached_bg_w || target_h != cached_bg_h || bg_resized_cache.empty()) {
            cv::resize(bg_image, bg_resized_cache, cv::Size(target_w, target_h));
            cached_bg_w = target_w;
            cached_bg_h = target_h;
        }
        return bg_resized_cache;
    } else if (bg_type == "video") {
        cv::Mat frame;
        bool ret = bg_video.read(frame);
        if (!ret) {
            bg_video.set(cv::CAP_PROP_POS_FRAMES, 0);
            ret = bg_video.read(frame);
            if (!ret) {
                return cv::Mat::zeros(target_h, target_w, CV_8UC3);
            }
        }
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(target_w, target_h));
        return resized;
    } else {
        return cv::Mat::zeros(target_h, target_w, CV_8UC3);
    }
}

// 核心：分割+背景替换+基础文字绘制
cv::Mat HumanSeg::segmentAndReplace(const cv::Mat& frame) {
    if (frame.empty()) {
        throw std::invalid_argument("input frame is empty!");
    }

    // 1. 预处理：BGR->RGB + resize + (x-127.5)/127.5 + HWC->NCHW
    //    blobFromImage 内部走 SIMD，比手写循环快一个数量级
    cv::Mat blob = cv::dnn::blobFromImage(
        frame,
        1.0 / 127.5,
        cv::Size(input_width, input_height),
        cv::Scalar(127.5, 127.5, 127.5),
        true,
        false,
        CV_32F
        );

    // 2. ONNX 推理（复用 memory_info）
    std::array<int64_t, 4> input_shape = {1, 3, input_height, input_width};
    Ort::Value input_tensor_obj = Ort::Value::CreateTensor<float>(
        memory_info,
        reinterpret_cast<float*>(blob.data),
        static_cast<size_t>(blob.total()),
        input_shape.data(), input_shape.size()
        );

    const char* input_name = "input";
    const char* output_name = "output";
    std::vector<Ort::Value> output_tensors = ort_session->Run(
        Ort::RunOptions{nullptr},
        &input_name, &input_tensor_obj, 1,
        &output_name, 1
        );

    // 3. 掩码：384x384 float -> 原图尺寸 8U(0/255)
    float* seg_data = output_tensors[0].GetTensorMutableData<float>();
    cv::Mat seg_map(input_height, input_width, CV_32F, seg_data);

    cv::Mat seg_map_resized;
    cv::resize(seg_map, seg_map_resized, cv::Size(frame.cols, frame.rows), 0, 0, cv::INTER_LINEAR);

    // 一次扫描把 CV_32F 概率图转成 CV_8U 二值掩码（>conf 为 255，否则 0），
    // 比原先 threshold + convertTo 两次扫描更快。
    cv::Mat person_mask;
    cv::compare(seg_map_resized, conf_threshold, person_mask, cv::CMP_GT);

    // 4. 取背景（图片背景命中缓存，clone 避免污染缓存）
    cv::Mat bg_frame = getBgFrame({frame.rows, frame.cols});
    cv::Mat output_frame = bg_frame.clone();

    // 5. 单次合成：把人像像素直接拷到背景上（一次扫描，零额外临时图）
    frame.copyTo(output_frame, person_mask);

    if (!title.empty()) {
        putText::putTextZH(
            output_frame, title.c_str(), Point(titleX, titleY),
            Scalar(std::get<2>(rgb), std::get<1>(rgb), std::get<0>(rgb)),
            font_size, font_name.c_str());
    }

    return output_frame;
}

// 释放资源
void HumanSeg::release() {
    qDebug() << "开始释放HumanSeg资源..." << '\n';

    // 1. 释放ONNX Session
    try {
        if (ort_session) {
            ort_session.reset();
            qDebug() << "ONNX会话已释放" << '\n';
        }
    } catch (const std::exception& e) {
        qDebug() << "ONNX会话释放警告：" << e.what() << '\n';
    }

    // 2. 释放视频资源
    try {
        if (bg_video.isOpened()) {
            bg_video.release();
            qDebug() << "背景视频已释放" << '\n';
        }
    } catch (const std::exception& e) {
        qDebug() << "背景视频释放警告：" << e.what() << '\n';
    }

    // 3. 释放图片资源
    bg_image.release();
    bg_type.clear();
     qDebug() << "HumanSeg资源释放完成" << '\n';
}
bool HumanSeg::isContainChineseUTF8(const std::string& utf8Str) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8Str.c_str());
    size_t len = utf8Str.size();
    size_t i = 0;

    while (i < len) {
        // 1. 跳过ASCII字符（0x00~0x7F）
        if (p[i] < 0x80) {
            i++;
            continue;
        }

        // 2. 检测UTF-8多字节序列（仅处理3字节的中文范围）
        // UTF-8 3字节格式：1110xxxx 10xxxxxx 10xxxxxx
        if ((p[i] >= 0xE4 && p[i] <= 0xE9) && (i + 2 < len)) {
            // 验证后续2个字节是否符合UTF-8格式（10xxxxxx）
            if ((p[i+1] >= 0x80 && p[i+1] <= 0xBF) && (p[i+2] >= 0x80 && p[i+2] <= 0xBF)) {
                // 计算对应的Unicode码点，验证是否在中文范围（0x4E00~0x9FFF）
                uint32_t unicode = ((p[i] & 0x0F) << 12) | ((p[i+1] & 0x3F) << 6) | (p[i+2] & 0x3F);
                if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
                    return true;
                }
                i += 3;
                continue;
            }
        }

        // 3. 处理其他UTF-8多字节（非中文，如emoji、日文等）
        if (p[i] >= 0xF0) { // 4字节UTF-8（跳过）
            i += 4;
        } else if (p[i] >= 0xE0) { // 3字节UTF-8（已处理中文，此处跳过）
            i += 3;
        } else if (p[i] >= 0xC0) { // 2字节UTF-8（非中文）
            i += 2;
        } else { // 无效UTF-8字节（跳过）
            i++;
        }
    }

    return false;
}
// 读取带中文路径的图片
cv::Mat HumanSeg::imreadChinese(const std::string& path, int flags) {
    if(isContainChineseUTF8(path)){
        std::filesystem::path fs_path(path);
        std::ifstream file(fs_path, std::ios::binary);
        // 2. 检查文件是否成功打开
        if (!file.is_open()) {
            return {}; // 返回空矩阵
        }

        // 3. 将文件的全部内容读入内存缓冲区 (vector<uchar>)
        // (使用 istreambuf_iterator "流" 式读取)
        const std::vector<uchar> buffer(std::istreambuf_iterator<char>(file), {});
        file.close();

        // 4. 检查缓冲区是否为空（文件是否为空或读取失败）
        if (buffer.empty()) {
            std::cerr << "Error: [imread_unicode] 文件为空: " << fs_path << std::endl;
            return {}; // 返回空矩阵
        }

        // 5. 使用 cv::imdecode 从内存缓冲区解码图像
        try {
            cv::Mat img = cv::imdecode(buffer, flags);
            if (img.empty()) {
                std::cerr << "Error: [imread_unicode] cv::imdecode 解码失败 (图像为空): " << fs_path << std::endl;
            }
            return img;
        } catch (const cv::Exception& ex) {
            std::cerr << "Error: [imread_unicode] cv::imdecode 失败: " << ex.what() << std::endl;
            return {};
        }
    }
    else{
        std::wstring wpath(path.begin(), path.end());
        FILE* fp = _wfopen(wpath.c_str(), L"rb");
        if (!fp) {
            return cv::Mat();
        }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        std::vector<char> buf(size);
        fread(buf.data(), 1, size, fp);
        fclose(fp);
        // 解码为Mat
        return cv::imdecode(cv::Mat(buf), flags);
    }
}

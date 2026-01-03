#ifndef HUMANSEG_H
#define HUMANSEG_H

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <tuple>
#include "puttext.h"
#include <QDebug>
#include <QStringConverter>
namespace fs = std::filesystem;
class HumanSeg {
public:
    /**
     * @brief 构造函数
     * @param conf_thres 分割置信度阈值
     */
    explicit HumanSeg(float conf_thres = 0.5f);

    /**
     * @brief 析构函数
     */
    ~HumanSeg();

    /**
     * @brief 设置背景（图片/视频）
     * @param bg_path 背景路径（支持中文）
     * @param bg_type 类型："image" / "video"
     */
    void setBackground(const std::string& bg_path, const std::string& bg_type);
    /**
     * @brief 人像分割+背景替换+基础文字绘制
     * @param frame 输入帧（BGR格式）
     * @return 处理后的帧
     */
    cv::Mat segmentAndReplace(const cv::Mat& frame);

    /**
     * @brief 释放所有资源
     */
    void release();

    /**
     * @brief 设置绘制的文字属性（仅支持ASCII字符）
     * @param title 文字内容（ASCII）
     * @param x 横坐标
     * @param y 纵坐标
     * @param font_size 字体大小
     * @param rgb 字体颜色（RGB）
     */
    void setTitle(const std::string& title, int x, int y, int font_size, std::tuple<int, int, int> rgb) {
        this->title = title;
        this->titleX = x;
        this->titleY = y;
        this->font_size = font_size;
        this->rgb = rgb;
    }
    void setTitle(const std::string& title) {
        this->title = title;
    }
    void setTitleX(int x){
        this->titleX=x;
    }
    void setTitleY(int y){
        this->titleY=y;
    }
    void setFontSize(int size){
        this->font_size=size;
    }
    void setRgb(std::tuple<int, int, int> rgb){
        this->rgb=rgb;
    }
    void setConfThreshold(float threshold){
        this->conf_threshold=threshold;
    }
    std::string getBgType(){
        return this->bg_type;
    }
private:
    bool isContainChineseUTF8(const std::string& utf8Str);
    /**
     * @brief 获取适配尺寸的背景帧
     * @param target_size 目标尺寸 (height, width)
     * @return 背景帧
     */
    cv::Mat getBgFrame(const std::pair<int, int>& target_size);

    /**
     * @brief 读取带中文路径的图片
     * @param path 图片路径
     * @param flags 读取标志（同cv::imread）
     * @return 图片Mat
     */
    cv::Mat imreadChinese(const std::string& path, int flags = cv::IMREAD_UNCHANGED);

    // ONNX Runtime 相关
    std::unique_ptr<Ort::Session> ort_session;
    Ort::Env env{OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "HumanSeg"};
    float conf_threshold;
    int input_height = 192;  // MODNet输入高度
    int input_width = 384;   // MODNet输入宽度

    // 预处理参数
    cv::Mat mean;
    cv::Mat std;

    // 背景相关
    std::string bg_type;
    cv::Mat bg_image;
    cv::VideoCapture bg_video;

    // 基础文字绘制相关（仅ASCII）
    std::string title = "Oath Slogan"; // 默认ASCII文字
    int titleX = 10;
    int titleY = 10;
    int font_size = 40;
    std::tuple<int, int, int> rgb = {0, 0, 0}; // 默认黑色
};

#endif // HUMANSEG_H

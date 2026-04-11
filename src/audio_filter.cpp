#include "audio_filter.h"
extern "C"{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}
AudioFilter::AudioFilter()
{
    graph = nullptr;
    src_ctx = nullptr;
    sink_ctx = nullptr;
}
AudioFilter::~AudioFilter()
{
    if(graph)
    {
        avfilter_graph_free(&graph);
    }
}
bool AudioFilter::init(AVCodecContext* dec_ctx, AVRational time_base) {
    int ret;

    // 分配一张全新的滤镜大网
    graph = avfilter_graph_alloc();
    if (!graph) return false;

    // 拿到我们要用的三个车间图纸
    const AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    const AVFilter *atempo      = avfilter_get_by_name("atempo");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");

    // ==========================================
    // 配置入口 (abuffer) - 极其严苛的参数拼接
    // ==========================================
    char args[512];
    
    // 获取声道布局字符串 (兼容 FFmpeg 5.1+ 新版 API)
    char ch_layout_str[64];
    av_channel_layout_describe(&dec_ctx->ch_layout, ch_layout_str, sizeof(ch_layout_str));

    // 严格按照解码器的参数，拼装入口描述：
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
             time_base.num, time_base.den,
             dec_ctx->sample_rate,
             av_get_sample_fmt_name(dec_ctx->sample_fmt),
             ch_layout_str);

    // 创建入口节点
    ret = avfilter_graph_create_filter(&src_ctx, abuffersrc, "in", args, NULL, graph);
    if (ret < 0) {
        return false;
    }

    // ==========================================
    // 配置变速滤镜 (atempo)
    // ==========================================
    AVFilterContext *atempo_ctx;
    // 第四个参数 "1.0" 是初始倍速
    ret = avfilter_graph_create_filter(&atempo_ctx, atempo, "atempo_filter", "1.0", NULL, graph);
    if (ret < 0) {
        return false;
    }

    // ==========================================
    // 配置出口 (abuffersink)
    // ==========================================
    ret = avfilter_graph_create_filter(&sink_ctx, abuffersink, "out", NULL, NULL, graph);
    if (ret < 0) return false;

    // 【超级重要】：强制 abuffersink 只能输出和重采样器 swr_ctx 完全一致的格式和采样率！
    // 否则滤镜网如果在内部协商成了 Interleaved (交错) 格式，喂给原本配成 Planar (平面) 的 swr 会导致空指针越界崩溃
    enum AVSampleFormat out_sample_fmts[] = { dec_ctx->sample_fmt, AV_SAMPLE_FMT_NONE };
    av_opt_set_int_list(sink_ctx, "sample_fmts", out_sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    int out_sample_rates[] = { dec_ctx->sample_rate, -1 };
    av_opt_set_int_list(sink_ctx, "sample_rates", out_sample_rates, -1, AV_OPT_SEARCH_CHILDREN);

    // ==========================================
    // 用水管把它们连接起来: in -> atempo -> out
    // ==========================================
    if ((ret = avfilter_link(src_ctx, 0, atempo_ctx, 0)) < 0) return false;
    if ((ret = avfilter_link(atempo_ctx, 0, sink_ctx, 0)) < 0) return false;

    // 激活整个加工厂！
    if ((ret = avfilter_graph_config(graph, NULL)) < 0) {
        return false;
    }

    return true;
}

int AudioFilter::add_frame(AVFrame* frame)
{
    if(!src_ctx)
    {
        return -1;
    }
    return av_buffersrc_add_frame(src_ctx, frame);
}

int AudioFilter::get_frame(AVFrame* frame)
{
    if(!sink_ctx)
    {
        return -1;
    }
    return av_buffersink_get_frame(sink_ctx, frame);
}

AVRational AudioFilter::get_out_time_base()
{
    if(!sink_ctx) {
        return {1, 48000}; 
    }
    return av_buffersink_get_time_base(sink_ctx);
}

void AudioFilter::set_speed(double speed)
{
    if(!graph)
    {
        return;
    }
    if (speed < 0.5) speed = 0.5;
    if (speed > 2.0) speed = 2.0;

    if (std::abs(speed - current_speed) < 0.01) return;
    current_speed = speed;

    char speed_str[32];
    snprintf(speed_str, sizeof(speed_str), "%.2f", speed);

    // 给名为 "atempo_filter" 的节点发送 "tempo" 指令，瞬间变道！
    avfilter_graph_send_command(graph, "atempo_filter", "tempo", speed_str, NULL, 0, AVFILTER_CMD_FLAG_ONE);
}
void AudioFilter::flush(AVCodecContext* dec_ctx, AVRational time_base) {
    if (graph) {
        avfilter_graph_free(&graph);
        src_ctx = nullptr;
        sink_ctx = nullptr;
    }
    init(dec_ctx, time_base);
}

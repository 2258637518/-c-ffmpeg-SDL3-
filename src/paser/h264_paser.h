#pragma once
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <vector>
struct Nalu {
	int start_code_len = 0;
	const uint8_t* data = nullptr;
	size_t size = 0;
	uint8_t type = 0;
};

struct SpsInfo {
    uint32_t profile_idc = 0;
    uint32_t level_idc = 0;
    uint32_t sps_id = 0;

    uint32_t width = 0;
    uint32_t height = 0;

    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), bit_pos_(0) {}

    // 1. 读取单个 bit
    uint32_t ReadBit() {
        if (bit_pos_ >= size_ * 8) {
            return 0; // 越界安全防护
        }

        size_t byte_idx = bit_pos_ / 8;
        size_t bit_offset = 7 - (bit_pos_ % 8);
        bit_pos_++;
        return (data_[byte_idx] >> bit_offset & 0x01);
    }

    // 2. 读取连续 n 个 bit 并拼装为整数
    uint32_t ReadBits(int n) {
        uint32_t val = 0;
        for (int i = 0; i < n; ++i) {
            val = (val << 1) | ReadBit();
        }
        return val;
    }

    // 辅助接口：获取当前总 bit 偏移
    size_t GetBitPos() const { return bit_pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bit_pos_; // 当前游标所在的 bit 偏移 (0, 1, 2...)
};

// 3. 🎯 0 阶无符号指数哥伦布解码
inline uint32_t ReadUe(BitReader& reader) {
    int leadingZeros = 0;

    // 统计前导 0 的个数，直到遇到 1 为止
    while (reader.ReadBit() == 0 && leadingZeros < 32)
    {
        leadingZeros++;
    }

    if (leadingZeros == 0)
    {
        return 0;
    }

    // 读取 leadingZeros 位长度的 INFO，并套用公式计算
    uint32_t info = reader.ReadBits(leadingZeros);
    return (1 << leadingZeros) - 1 + info;
}
inline int MatchStartCode(const uint8_t* data, size_t bytes_remaining)
{
    if (bytes_remaining < 3) {
        return 0;
    }

    if (bytes_remaining>=4 && data[0]== 0x00 && data[1]== 0x00 && data[2]== 0x00 &&data[3]== 0x01) {
        return 4;
    }

    if (bytes_remaining >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        return 3;
    }

    return 0;
}

inline int FindNextStartCode(const uint8_t* data, size_t size, int& start_code_len) {
    if (!data || size < 3) {
        return -1;
    }

    for (size_t i = 0; i <= size - 3; ++i) {
        int len = MatchStartCode(data+i, size - i);
        if (len > 0) {
            start_code_len = len;
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * @brief 将一段 H.264 Annex B 字节流切分为独立的 NALU 列表
 * @param data 裸流内存首地址
 * @param size 裸流总字节数
 * @return 切分好的 Nalu 结构体列表
 */
inline std::vector<Nalu> SplitNalu(const uint8_t* data, size_t size) {
    std::vector<Nalu> nalu_list;
    if (!data || size < 4) return nalu_list;

    // 1. 找到整个流中的第一个起始码
    int first_code_len = 0;
    int first_pos = FindNextStartCode(data, size, first_code_len);
    if (first_pos < 0) return nalu_list; // 连一个起始码都没有，无效流

    int cur_start = first_pos;
    int cur_code_len = first_code_len;

    // 2. 循环寻找下一个起始码，切分出每一个 NALU
    while (cur_start < static_cast<int>(size)) {
        // 从当前起始码之后的位置开始，寻找下一个起始码
        int next_code_len = 0;
        int next_search_offset = cur_start + cur_code_len;

        int next_pos = -1;
        if (next_search_offset < static_cast<int>(size)) {
            next_pos = FindNextStartCode(data + next_search_offset, size - next_search_offset, next_code_len);
            if (next_pos >= 0) {
                // 转换回相对于 data 开头的绝对偏移
                next_pos += next_search_offset;
            }
        }

        // 计算当前 NALU 的有效载荷区间
        Nalu nalu;
        nalu.start_code_len = cur_code_len;
        nalu.data = data + cur_start + cur_code_len; // 指向 NALU Header

        if (next_pos >= 0) {
            nalu.size = next_pos-next_search_offset;
        }
        else {
            nalu.size = size - next_search_offset;
        }

        if (nalu.size > 0) {
            nalu.type = nalu.data[0] & 0x1F;
            nalu_list.push_back(nalu);
        }

        if (next_pos < 0) break; // 已经处理完最后一个 NALU

        // 步进到下一个 NALU
        cur_start = next_pos;
        cur_code_len = next_code_len;
    }

    return nalu_list;
}

inline std::vector<uint8_t> RemoveEmulationPreventionBytes(const uint8_t* ebsp_data, size_t ebsp_size) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(ebsp_size);
    size_t i = 0;
    while (i < ebsp_size) {
        // 判断是否满足防竞争条件: 前两字节为 0x00, 0x00，第 3 字节为 0x03
        if (i + 2 < ebsp_size && ebsp_data[i] == 0 && ebsp_data[i + 1] == 0 && ebsp_data[i + 2] == 3)
        {
            rbsp.push_back(ebsp_data[i]);
            rbsp.push_back(ebsp_data[i + 1]);
            i += 3; // 跳过 0x03
        }
        else
        {
            rbsp.push_back(ebsp_data[i]);
            i++;
        }
    }

    return rbsp;
}
/**
     * @brief 解析 H.264 SPS 语法树头部基础字段
     * @param rbsp_data 已经完成 0x03 脱壳的 RBSP 数据首地址（注意：不含 NALU Header）
     * @param rbsp_size RBSP 数据总字节数
     */
inline bool ParseSpsBasic(const uint8_t* rbsp_data, size_t rbsp_size, SpsInfo& sps) {
    if (!rbsp_data || rbsp_size < 3) {
        return false;
    }

    BitReader reader(rbsp_data, rbsp_size);

    sps.profile_idc = reader.ReadBits(8);
    reader.ReadBits(8); // 跳过 constraint_set_flags 与保留位
    sps.level_idc = reader.ReadBits(8);
    sps.sps_id = ReadUe(reader);

    // 1. 读取帧序号与 POC 相关字段（过路参数）
    uint32_t log2_max_frame_num_minus4 = ReadUe(reader);
    uint32_t pic_order_cnt_type = ReadUe(reader);

    if (pic_order_cnt_type == 0) {
        uint32_t log2_max_pic_order_cnt_lsb_minus4 = ReadUe(reader);
    }

    // 2. 读取参考帧数与间隙标志
    uint32_t max_num_ref_frames = ReadUe(reader);
    uint32_t gaps_in_frame_num_value_allowed_flag = reader.ReadBit();

    // 3. 读取宏块宽高与帧场标志位
    uint32_t pic_width_in_mbs_minus1 = ReadUe(reader);
    uint32_t pic_height_in_map_units_minus1 = ReadUe(reader);
    uint32_t frame_mbs_only_flag = reader.ReadBit();

    // 4. 计算基础未裁切宽高
    sps.width = (pic_width_in_mbs_minus1 + 1) * 16;
    sps.height = frame_mbs_only_flag == 0 ? (pic_height_in_map_units_minus1 + 1) * 32 : (pic_height_in_map_units_minus1 + 1) * 16;

    // 5. 补充场编码判断与 8x8 推导标志
    if (!frame_mbs_only_flag) {
        reader.ReadBit(); // mb_adaptive_frame_field_flag
    }
    reader.ReadBit(); // direct_8x8_inference_flag

    // 6. 画面裁切判定 (frame_cropping_flag)
    uint32_t frame_cropping_flag = reader.ReadBit();

    if (frame_cropping_flag) {
        sps.crop_left = ReadUe(reader);
        sps.crop_right = ReadUe(reader);
        sps.crop_top = ReadUe(reader);
        sps.crop_bottom = ReadUe(reader);

        // 计算裁切单位 (基于 YUV420P)
        uint32_t crop_unit_x = 2;
        uint32_t crop_unit_y = 2 * (2 - frame_mbs_only_flag);

        // 扣除裁切黑边，更新 sps.width 和 sps.height
        sps.width -= (sps.crop_left + sps.crop_right) * crop_unit_x;
        sps.height -= (sps.crop_top + sps.crop_bottom) * crop_unit_y;
    }
    return true;
}
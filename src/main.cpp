#include "player.h"
#include "paser/h264_paser.h"
#ifdef _WIN32
#include <windows.h>
#endif

void test_h264_paser()
{
    // 1. 真实 5、WLOP.mp4 的 SPS 裸数据
    uint8_t sps_1080p_ebsp[] = {
        0x67, 0x4D, 0x40, 0x29, 0xEC, 0xA0, 0x3C, 0x01,
        0x13, 0xF2, 0xE0, 0x22, 0x00, 0x00, 0x03, 0x00,
        0x02, 0x00, 0x00, 0x03, 0x00, 0x64, 0x1E, 0x30,
        0x63, 0x2C
    };

    // 2. 先脱壳：剔除其中的 0x00 0x00 0x03 防竞争字节
    std::vector<uint8_t> rbsp = RemoveEmulationPreventionBytes(sps_1080p_ebsp, sizeof(sps_1080p_ebsp));

    // 3. 解析 SPS
    // 注意：传入 rbsp.data() + 1，跳过第 0 字节的 0x67 (NALU Header)！
    SpsInfo sps;
    if (ParseSpsBasic(rbsp.data() + 1, rbsp.size() - 1, sps)) {
        printf("\n========================================\n");
        printf("🎉 自研 SPS 解析器推导结果:\n");
        printf("Profile IDC : %u (77 代表 Main Profile)\n", sps.profile_idc);
        printf("Level IDC   : %u (51 代表 Level 5.1)\n", sps.level_idc);
        printf("Video Width : %u px\n", sps.width);
        printf("Video Height: %u px\n", sps.height);
        printf("========================================\n\n");
    }

}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // 设置控制台输出代码页为 UTF-8 (65001)，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
#endif

    (void)argc;//控制台编译成功时输入的参数个数
    (void)argv;//在命令行中输入的附加参数
    const std::string filepath = "E:/Pictures/5、WLOP.mp4";

    test_h264_paser();

    Player player(filepath);

    if (!player.open()) {
        return -1;
    }

    player.start();

    while (!player.should_quit()) {
        player.tick();
    }

    player.stop();
    return 0;
}

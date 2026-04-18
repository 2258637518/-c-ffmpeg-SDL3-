#include "player.h"
#include <iostream>

int main(int argc, char* argv[])
{
    // 如果没有输入文件路径，输出帮助信息并退出
    if (argc < 2) {
        std::cerr << "使用方法: " << argv[0] << " <视频文件路径>" << std::endl;
        return -1;
    }

    // 获取命令行传入的第一个参数作为文件路径
    const std::string filepath = argv[1];

    Player player(filepath);
    if (!player.open()) {
        std::cerr << "播放器打开失败，请检查文件路径是否正确: " << filepath << std::endl;
        return -1;
    }

    player.start();

    while (!player.should_quit()) {
        player.tick();
    }

    player.stop();
    return 0;
}

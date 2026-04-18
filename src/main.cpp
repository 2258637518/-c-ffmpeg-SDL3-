#include "player.h"
int main(int argc, char* argv[])
{
    (void)argc;//控制台编译成功时输入的参数个数
    (void)argv;//在命令行中输入的附加参数
    const std::string filepath = "/home/gloomybunny/video/GTA6_Trailer2.mp4";

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

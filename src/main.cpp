#include "player.h"
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

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

module;

export module Apps.ParseCli;

namespace Extrinsic::Apps
{
    export struct ParseCLIResult
    {
        //Todo figure out how and what to control on startup
    };

    export ParseCLIResult ParseCli(int argc, char** argv);
}
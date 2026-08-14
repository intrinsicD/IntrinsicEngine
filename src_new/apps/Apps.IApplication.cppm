module;

export module Apps.IApplication;

import Apps.ParseCli;

namespace Extrinsic::Apps
{
    export class IApplication
    {
    public:
        virtual ~IApplication() = default;

    protected:
        IApplication(const ParseCLIResult& cliResult);
    };
}
module;

export module Apps.Sandbox;

import Apps.IApplication;
import Apps.ParseCli;

namespace Extrinsic::Apps
{
    export class Sandbox : public IApplication
    {
    public:
        Sandbox(const ParseCLIResult& cliResult) : IApplication(cliResult), mCliResult(cliResult)
        {
        }

        ~Sandbox() override;

        Sandbox(const Sandbox&) = delete;
        Sandbox& operator=(const Sandbox&) = delete;

    private:
        const ParseCLIResult& mCliResult;
    };
}

import Apps.Sandbox;
import Apps.ParseCli;
import Runtime.Engine;

int main(int argc, char** argv)
{
    Extrinsic::ParseCLIResult result = ParseCli(argc, argv);
    Extrinsic::Apps::Sandbox application(result);
    Extrinsic::Platform::IBackend* pPlatformModule = DeterminePlatformModule(&application, result);
    Extrinsic::Graphics::IBackend* pGraphicsBackend = DetermineGraphicsBackend(&application, result);
    Extrinsic::Compute::IBackend* pComputeBackend = DetermineComputeBackend(&application, result);
    Extrinsic::ECS::World* pWorld = DetermineWorld(&application, result);
    Extrinsic::Runtime::Engine engine({&application, pPlatformModule, pGraphicsBackend, pComputeBackend, pWorld});
    return 0;
}
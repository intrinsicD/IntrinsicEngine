#include "Test.CoreFrameGraphTypeTokenHelper.hpp"

// Second translation unit for the cross-TU identity check. It imports the
// token's owner directly; the test TU reaches the same entity while also
// importing Extrinsic.Core.FrameGraph.
import Extrinsic.Core.Hash;

size_t GetFrameGraphSharedTypeTokenFromHelperTU()
{
    return Extrinsic::Core::TypeToken<FrameGraphSharedTypeTokenFixtureType>();
}

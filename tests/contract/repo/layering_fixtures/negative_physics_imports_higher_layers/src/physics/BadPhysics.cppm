// Negative fixture: physics must not import ECS, runtime, graphics, platform, or app layers.
export module Fixture.BadPhysics;

import Extrinsic.ECS.Events;
import Extrinsic.Runtime.Engine;
import Extrinsic.Graphics.Pass.Surface;
import Extrinsic.Platform.Window;
import Extrinsic.App.Sandbox;

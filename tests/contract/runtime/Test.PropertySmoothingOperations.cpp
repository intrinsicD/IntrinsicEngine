// Canonical domain/type coverage, publication, config validation and guarded undo.
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "PointDomainFixture.hpp"
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Extrinsic.ECS.Components.GeometrySourcesPopulate;
namespace R = Extrinsic::Runtime;
namespace GS = Extrinsic::ECS::Components::GeometrySources;
namespace S = Geometry::Smoothing;
using D = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct SmoothingHarness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        R::PropertySmoothingConfig Config;
        explicit SmoothingHarness(D domain = D::MeshVertex)
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, domain);
            Config.Input = {domain,"temperature",K::Double};
            Config.Output = {domain,"smooth",K::Double};
            Config.Positions = {domain,"samples",K::Vec3};
            auto& props = Props();
            auto samples = props.GetOrAdd<glm::vec3>("samples", {});
            auto values = props.GetOrAdd<double>("temperature", 0.);
            for (std::size_t i=0;i<props.Size();++i) { samples[i]={float(i),0,0}; values[i]=double(i); }
            Context.Scene=&Scene; Context.CommandHistory=&History;
        }
        Geometry::PropertySet& Props() { return Intrinsic::Tests::PointDomainProperties(Scene,Entity,Config.Input.Domain); }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        auto Run() { return R::ApplyEditorPropertySmoothingCommand(Commands(),Id(),Config); }
    };
    constexpr std::array domains{D::MeshVertex,D::MeshEdge,D::MeshHalfedge,D::MeshFace,D::GraphNode,D::GraphEdge,D::GraphHalfedge,D::PointCloudPoint};
}
TEST(PropertySmoothingOperations, AllDomainsAndFiltersPublishWithUndoRedo)
{
    for (auto domain : domains)
        for (auto method : {S::PropertyFilter::Averaging,S::PropertyFilter::SpectralHeat,S::PropertyFilter::Taubin,S::PropertyFilter::Bilateral,S::PropertyFilter::Implicit,S::PropertyFilter::VariationalFit})
        {
            SCOPED_TRACE(int(domain));
            SCOPED_TRACE(int(method));
            SmoothingHarness h(domain); h.Config.Filter.Method=method;
            (void)h.Props().GetOrAdd<float>("unrelated",7.f);
            const auto before=std::as_const(h.Props()).Get<double>("temperature").Vector();
            const auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_EQ(result.LiveCount,h.Props().Size());
            EXPECT_EQ(std::as_const(h.Props()).Get<double>("temperature").Vector(),before);
            EXPECT_EQ(std::as_const(h.Props()).Get<float>("unrelated")[0],7.f);
            const auto output=std::as_const(h.Props()).Get<double>("smooth").Vector();
            ASSERT_TRUE(h.History.Undo().Succeeded()); EXPECT_FALSE(h.Props().Exists("smooth"));
            ASSERT_TRUE(h.History.Redo().Succeeded());
            EXPECT_EQ(std::as_const(h.Props()).Get<double>("smooth").Vector(),output);
        }
}
TEST(PropertySmoothingOperations, AllFloatingKindsAcrossAllDomains)
{
    for (auto domain : domains)
        for (auto method : {S::PropertyFilter::Averaging,S::PropertyFilter::Implicit})
        for (auto kind : {K::Float,K::Double,K::Vec2,K::Vec3,K::Vec4})
        {
            SCOPED_TRACE(int(domain));
            SCOPED_TRACE(int(kind));
            SmoothingHarness h(domain);
            h.Config.Filter.Method=method;
            h.Config.Input.Name="field"; h.Config.Input.ValueKind=h.Config.Output.ValueKind=kind;
            auto& p=h.Props();
            h.Config.Filter.TimeStep=double(p.Size()-1)/double(p.Size()-2);
            if(kind==K::Float) p.GetOrAdd<float>("field",3.f)[0]=0;
            if(kind==K::Double) p.GetOrAdd<double>("field",3.)[0]=0;
            if(kind==K::Vec2) p.GetOrAdd<glm::vec2>("field",glm::vec2(3))[0]=glm::vec2(0);
            if(kind==K::Vec3) p.GetOrAdd<glm::vec3>("field",glm::vec3(3))[0]=glm::vec3(0);
            if(kind==K::Vec4) p.GetOrAdd<glm::vec4>("field",glm::vec4(3))[0]=glm::vec4(0);
            auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
            EXPECT_TRUE(p.Exists("smooth"));
            if(kind==K::Float) EXPECT_FLOAT_EQ(std::as_const(p).Get<float>("smooth")[0],1.5f);
            if(kind==K::Double) EXPECT_NEAR(std::as_const(p).Get<double>("smooth")[0],1.5,1e-9);
            if(kind==K::Vec2) EXPECT_EQ(std::as_const(p).Get<glm::vec2>("smooth")[0],glm::vec2(1.5f));
            if(kind==K::Vec3) EXPECT_EQ(std::as_const(p).Get<glm::vec3>("smooth")[0],glm::vec3(1.5f));
            if(kind==K::Vec4) EXPECT_EQ(std::as_const(p).Get<glm::vec4>("smooth")[0],glm::vec4(1.5f));
        }
}
TEST(PropertySmoothingOperations, OverwriteInputAndPositionsRemainUndoable)
{
    for (bool positions : {false,true})
    {
        SmoothingHarness h;
        if(positions) h.Config.Input=h.Config.Positions;
        h.Config.Output=h.Config.Input;
        const auto before=std::as_const(h.Props()).Get<glm::vec3>("samples").Vector();
        auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
        ASSERT_TRUE(h.History.Undo().Succeeded());
        EXPECT_EQ(std::as_const(h.Props()).Get<glm::vec3>("samples").Vector(),before);
        ASSERT_TRUE(h.History.Redo().Succeeded());
    }
}
TEST(PropertySmoothingOperations, DeletedRowsAndNonfiniteLiveValues)
{
    SmoothingHarness h;
    auto& p=h.Props();
    p.GetOrAdd<bool>("v:deleted",false)[1]=true;
    p.Get<double>("temperature")[1]=std::numeric_limits<double>::quiet_NaN();
    (void)p.GetOrAdd<double>("smooth",91.);
    auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.LiveCount,p.Size()-1);
    EXPECT_EQ(std::as_const(p).Get<double>("smooth")[1],91.);
    p.Get<double>("temperature")[0]=std::numeric_limits<double>::infinity();
    const auto before=std::as_const(p).Get<double>("smooth").Vector();
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_EQ(std::as_const(p).Get<double>("smooth").Vector(),before);
}
TEST(PropertySmoothingOperations, RejectsStaleHistoryAndInvalidBindings)
{
    SmoothingHarness h;
    ASSERT_TRUE(h.Run().Succeeded());
    h.Props().Get<double>("temperature")[0]=99;
    EXPECT_FALSE(h.History.Undo().Succeeded());
    h.Config.Output.Domain=D::MeshFace;
    EXPECT_FALSE(h.Run().Succeeded());
    h.Config.Output.Domain=h.Config.Input.Domain;
    h.Config.Output.Name="v:deleted";
    EXPECT_FALSE(h.Run().Succeeded());
    R::EditorProcessingContext empty;
    EXPECT_FALSE(R::ApplyEditorPropertySmoothingCommand(R::BindEditorProcessingCommands(empty),0,{}).Succeeded());
}
TEST(PropertySmoothingOperations, CotangentReusesMeshTopology)
{
    SmoothingHarness h;
    h.Config.Weight=S::PropertyWeight::Cotangent;
    h.Config.Positions.Name="v:position";
    auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_GT(result.EdgeCount,0u);
}
TEST(PropertySmoothingOperations, ConfigRoundtripAndFailClosedValidation)
{
    const auto registration=R::MakePropertySmoothingConfigSectionRegistration();
    R::PropertySmoothingConfig c;
    c.Input={D::GraphEdge,"custom",K::Vec4}; c.Output={D::GraphEdge,"filtered",K::Vec4};
    c.Positions={D::GraphEdge,"samples",K::Vec3}; c.Filter.Method=S::PropertyFilter::Implicit;
    c.Filter.TimeStep=12; c.Filter.SolverTolerance=1e-7; c.Filter.MaxSolverIterations=432;
    c.Filter.Solver=S::PropertySolver::ConjugateGradient;
    const auto payload=R::SerializePropertySmoothingConfig(c);
    EXPECT_EQ(payload.find("invalid"),std::string::npos);
    const auto valid=registration.Validate(payload,{},R::kPropertySmoothingConfigSectionName);
    ASSERT_TRUE(valid.Usable()); EXPECT_EQ(valid.CanonicalPayloadJson,payload);
    for (const char* invalid : {"{\"method\":256}","{\"iterations\":0}","{\"lambda\":2}","{\"mu\":0}","{\"heat_time\":null}","{\"unknown\":1}","{\"time_step\":0}","{\"solver_tolerance\":-1}","{\"max_solver_iterations\":0}","{\"preserve_boundary\":1}","{\"laplacian\":2}","{\"solver\":2}","{\"solver\":-1}"})
        EXPECT_FALSE(registration.Validate(invalid,{},R::kPropertySmoothingConfigSectionName).Usable()) << invalid;
}
TEST(PropertySmoothingOperations, DerivedFaceEdgeAndHalfedgeSamples)
{
    for(auto domain : {D::MeshFace,D::MeshEdge,D::MeshHalfedge,D::GraphEdge,D::GraphHalfedge})
    {
        SCOPED_TRACE(int(domain));
        SmoothingHarness h(domain);
        if(domain==D::MeshFace) h.Props().Resize(2);
        h.Config.Positions={domain==D::GraphEdge || domain==D::GraphHalfedge ? D::GraphNode : D::MeshVertex,"v:position",K::Vec3};
        auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.LiveCount,h.Props().Size());
        ASSERT_TRUE(h.History.Undo().Succeeded());
        ASSERT_TRUE(h.History.Redo().Succeeded());
    }
}
TEST(PropertySmoothingOperations, HalfedgeDeletionUsesPairedEdgeMask)
{
    SmoothingHarness h(D::GraphHalfedge);
    auto& edges=Intrinsic::Tests::PointDomainProperties(h.Scene,h.Entity,D::GraphEdge);
    edges.GetOrAdd<bool>("e:deleted",false)[0]=true;
    h.Props().Get<double>("temperature")[0]=std::numeric_limits<double>::quiet_NaN();
    h.Props().Get<double>("temperature")[1]=std::numeric_limits<double>::quiet_NaN();
    (void)h.Props().GetOrAdd<double>("smooth",123.);
    auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(result.LiveCount,h.Props().Size()-2);
    const auto output=std::as_const(h.Props()).Get<double>("smooth");
    EXPECT_EQ(output[0],123.); EXPECT_EQ(output[1],123.);
}
TEST(PropertySmoothingOperations, ScalarConversionAndOverflowAreTransactional)
{
    SmoothingHarness h;
    h.Config.Output.ValueKind=K::Float;
    ASSERT_TRUE(h.Run().Succeeded());
    ASSERT_TRUE(std::as_const(h.Props()).Get<float>("smooth"));
    h.Props().Get<double>("temperature").Vector().assign(h.Props().Size(),1e100);
    const auto before=std::as_const(h.Props()).Get<float>("smooth").Vector();
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_EQ(std::as_const(h.Props()).Get<float>("smooth").Vector(),before);
}

TEST(PropertySmoothingOperations, RejectsMalformedPropertyStorageBeforeAccess)
{
    SmoothingHarness h;
    h.Props().Get<double>("temperature").Vector().resize(1);
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_FALSE(h.Props().Exists("smooth"));
}

TEST(PropertySmoothingOperations, DetachedHistoryCannotPublish)
{
    SmoothingHarness h;
    bool attached=true;
    h.Context.AttachmentActive=[&] { return attached; };
    ASSERT_TRUE(h.Run().Succeeded());
    const auto output=std::as_const(h.Props()).Get<double>("smooth").Vector();
    attached=false;
    EXPECT_FALSE(h.History.Undo().Succeeded());
    EXPECT_EQ(std::as_const(h.Props()).Get<double>("smooth").Vector(),output);
}

TEST(PropertySmoothingOperations, ImplicitMeshPositionsPinBoundaryAndUndo)
{
    for (auto weight : {S::PropertyWeight::Cotangent,S::PropertyWeight::MeshUniform})
    {
        SmoothingHarness h;
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto a=mesh.AddVertex({0,0,0}), b=mesh.AddVertex({2,0,0}),
            c=mesh.AddVertex({2,2,0}), d=mesh.AddVertex({0,2,0}), center=mesh.AddVertex({1,1,1});
        ASSERT_TRUE(mesh.AddTriangle(a,b,center)); ASSERT_TRUE(mesh.AddTriangle(b,c,center));
        ASSERT_TRUE(mesh.AddTriangle(c,d,center)); ASSERT_TRUE(mesh.AddTriangle(d,a,center));
        GS::PopulateFromMesh(h.Scene.Raw(),h.Entity,mesh);
        h.Config.Input=h.Config.Output=h.Config.Positions={D::MeshVertex,"v:position",K::Vec3};
        h.Config.Filter.Method=S::PropertyFilter::Implicit;
        h.Config.Filter.Laplacian=S::PropertyLaplacian::LumpedMass;
        h.Config.Filter.TimeStep=100;
        h.Config.Filter.Iterations=3;
        h.Config.PreserveBoundary=true;
        h.Config.Weight=weight;
        const auto before=std::as_const(h.Props()).Get<glm::vec3>("v:position").Vector();
        const auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.BackendId,"cpu_sparse_cholesky");
        const auto after=std::as_const(h.Props()).Get<glm::vec3>("v:position").Vector();
        for (std::size_t i=0;i<4;++i) EXPECT_EQ(after[i],before[i]);
        EXPECT_LT(after[4].z,0.001f);
        EXPECT_NEAR(after[4].x,1,1e-6); EXPECT_NEAR(after[4].y,1,1e-6);
        ASSERT_TRUE(h.History.Undo().Succeeded());
        EXPECT_EQ(std::as_const(h.Props()).Get<glm::vec3>("v:position").Vector(),before);
        ASSERT_TRUE(h.History.Redo().Succeeded());
        EXPECT_EQ(std::as_const(h.Props()).Get<glm::vec3>("v:position").Vector(),after);
    }
}

TEST(PropertySmoothingOperations, ImplicitNonconvergenceLeavesOutputAndHistoryUntouched)
{
    SmoothingHarness h;
    h.Config.Filter.Method=S::PropertyFilter::Implicit;
    h.Config.Filter.Solver=S::PropertySolver::ConjugateGradient;
    h.Config.Filter.MaxSolverIterations=1;
    h.Config.Filter.SolverTolerance=1e-14;
    h.Config.Weight=S::PropertyWeight::MeshUniform;
    h.Config.Positions.Name="v:position";
    (void)h.Props().GetOrAdd<double>("smooth",91.);
    const auto before=std::as_const(h.Props()).Get<double>("smooth").Vector();
    EXPECT_FALSE(h.Run().Succeeded());
    EXPECT_EQ(std::as_const(h.Props()).Get<double>("smooth").Vector(),before);
    EXPECT_FALSE(h.History.Undo().Succeeded());
}

TEST(PropertySmoothingOperations, VariationalFitBindsPerRowBoundsAndReportsTheFit)
{
    for (auto domain : {D::MeshVertex,D::GraphEdge,D::PointCloudPoint})
    {
        SCOPED_TRACE(int(domain));
        SmoothingHarness h(domain);
        auto& props=h.Props();
        auto radii=props.GetOrAdd<float>("tolerance",0.25f);
        radii[0]=0.f;
        h.Config.Filter.Method=S::PropertyFilter::VariationalFit;
        h.Config.Filter.SmoothnessPenalty=S::FitPenalty::L1;
        h.Config.Filter.PenaltyDelta=1e-6;
        h.Config.Filter.FitWeight=0.01;
        h.Config.Filter.Bound=S::FitBound::PerRow;
        h.Config.BoundRadii={domain,"tolerance",K::Float};
        const auto input=std::as_const(props).Get<double>("temperature").Vector();
        const auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
        EXPECT_EQ(result.BackendId,"cpu_reference_sparse_cholesky");
        EXPECT_GT(result.ActiveBounds,0u);
        EXPECT_GT(result.OperatorApplications,1u);
        EXPECT_NEAR(result.FitWeight,0.01,1e-15);
        const auto output=std::as_const(props).Get<double>("smooth").Vector();
        EXPECT_EQ(output[0],input[0]) << "a zero radius pins the row";
        for (std::size_t i=0;i<output.size();++i) EXPECT_LE(std::abs(output[i]-input[i]),0.25+1e-9);
        // The radius property guards the publication like the input does.
        radii[1]=0.5f;
        EXPECT_FALSE(h.History.Undo().Succeeded());
        EXPECT_EQ(std::as_const(props).Get<double>("smooth").Vector(),output);
        radii[1]=-1.f;
        EXPECT_FALSE(h.Run().Succeeded());
        h.Config.BoundRadii.Name="missing";
        EXPECT_FALSE(R::PreviewEditorPropertySmoothingCommand(h.Commands(),h.Id(),h.Config).Enabled);
    }
}
TEST(PropertySmoothingOperations, VariationalFitNoiseLevelMatchesTheResidual)
{
    SmoothingHarness h;
    h.Config.Filter.Method=S::PropertyFilter::VariationalFit;
    h.Config.Filter.Fidelity=S::FitFidelity::NoiseLevel;
    h.Config.Filter.NoiseLevel=0.2;
    h.Config.Filter.DataPenalty=S::FitPenalty::Huber;
    h.Config.Filter.PenaltyDelta=0.5;
    const auto result=h.Run(); ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_NEAR(result.RmsResidual,0.2,0.2*2e-4);
    EXPECT_NE(result.Message.find("RMS residual"),std::string::npos);
}
TEST(PropertySmoothingOperations, VariationalFitConfigRoundtripAndValidation)
{
    const auto registration=R::MakePropertySmoothingConfigSectionRegistration();
    R::PropertySmoothingConfig c;
    c.Filter.Method=S::PropertyFilter::VariationalFit;
    c.Filter.SmoothnessPenalty=S::FitPenalty::Huber; c.Filter.DataPenalty=S::FitPenalty::L1;
    c.Filter.Fidelity=S::FitFidelity::NoiseLevel; c.Filter.FitWeight=3; c.Filter.NoiseLevel=0.02;
    c.Filter.PenaltyDelta=0.004; c.Filter.Bound=S::FitBound::PerRow; c.Filter.BoundRadius=0.7;
    c.Filter.MaxFitIterations=77; c.Filter.FitTolerance=1e-9; c.Filter.Laplacian=S::PropertyLaplacian::LumpedMass;
    c.BoundRadii={D::MeshVertex,"v:tolerance",K::Double};
    const auto payload=R::SerializePropertySmoothingConfig(c);
    const auto valid=registration.Validate(payload,{},R::kPropertySmoothingConfigSectionName);
    ASSERT_TRUE(valid.Usable()) << (valid.Diagnostics.empty() ? "" : valid.Diagnostics.front().Message);
    EXPECT_EQ(valid.CanonicalPayloadJson,payload);
    EXPECT_NE(R::SerializePropertySmoothingConfig({}).find("\"bound_radii\":null"),std::string::npos);
    for (const char* invalid : {"{\"method\":6}","{\"smoothness_penalty\":3}","{\"data_penalty\":3}","{\"fidelity\":2}",
             "{\"bound\":3}","{\"fit_weight\":0}","{\"noise_level\":-1}","{\"penalty_delta\":0}","{\"bound_radius\":-0.5}",
             "{\"max_fit_iterations\":0}","{\"fit_tolerance\":1}","{\"bound_radii\":5}",
             "{\"method\":5,\"bound\":2}"})
        EXPECT_FALSE(registration.Validate(invalid,{},R::kPropertySmoothingConfigSectionName).Usable()) << invalid;
    // Unused per-row radius bindings are kept but not required.
    EXPECT_TRUE(registration.Validate("{\"method\":5,\"bound\":1}",{},R::kPropertySmoothingConfigSectionName).Usable());
}

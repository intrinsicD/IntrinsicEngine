// Extracts vector-field overlay packets from Appearance layers, with
// per-entity anchor and payload caches keyed by source property revisions.
// Explicit `VectorFieldVisualizationRecipe`s keep the copying recipe encoder.
module;

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.RenderExtraction;

import :Internal;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PN = GS::PropertyNames;

        // Residency rejects a stamp lower than the one it holds for a key, so
        // every cache rebuild draws from one process-wide increasing sequence;
        // a recreated entity or cache can never reuse an older stamp.
        std::atomic<std::uint64_t> g_NextVectorFieldCacheStamp{1u};

        [[nodiscard]] std::uint64_t NextVectorFieldCacheStamp() noexcept
        {
            return g_NextVectorFieldCacheStamp.fetch_add(1u, std::memory_order_relaxed);
        }

        [[nodiscard]] bool IsFinite(const glm::vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] Graphics::VisualizationAttributeDomain ToAttributeDomain(
            const GeometryElementDomain domain) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshEdge:
            case GeometryElementDomain::GraphEdge:
                return Graphics::VisualizationAttributeDomain::Edge;
            case GeometryElementDomain::MeshFace:
                return Graphics::VisualizationAttributeDomain::Face;
            default:
                return Graphics::VisualizationAttributeDomain::Vertex;
            }
        }

        [[nodiscard]] bool IsPointDomain(const GeometryElementDomain domain) noexcept
        {
            return domain == GeometryElementDomain::MeshVertex ||
                   domain == GeometryElementDomain::GraphNode ||
                   domain == GeometryElementDomain::PointCloudPoint;
        }

        [[nodiscard]] GeometryElementDomain PositionDomainFor(
            const GeometryElementDomain domain) noexcept
        {
            return domain == GeometryElementDomain::GraphEdge
                ? GeometryElementDomain::GraphNode
                : GeometryElementDomain::MeshVertex;
        }

        [[nodiscard]] std::string BuildKey(const std::uint32_t stableId,
                                           const std::string_view lane,
                                           const GeometryElementDomain domain,
                                           const std::string_view name)
        {
            std::string key = std::to_string(stableId);
            key += ':';
            key += lane;
            key += '.';
            key += ToString(domain);
            key += ':';
            key += name;
            return key;
        }

        [[nodiscard]] Geometry::PropertyRevision RevisionOf(
            const Geometry::PropertySet* properties,
            const std::string_view name) noexcept
        {
            return properties != nullptr
                ? properties->FindPropertyRevision(name).value_or(0u)
                : 0u;
        }

        // One enabled Appearance layer, flattened for extraction.
        struct VectorFieldRequest
        {
            GeometryElementDomain Domain{GeometryElementDomain::Unknown};
            std::string Property{};
            std::string Name{};
            float Scale{1.0f};
            bool Normalize{true};
            float LineWidthPx{2.0f};
            glm::vec4 Color{1.0f};
            bool DepthTested{true};
            std::uint32_t Stride{1u};
            std::uint32_t MaxGlyphs{0u};
        };

        // Rows whose `deleted` flag is set are dead; a missing flag means all live.
        void CollectLiveRows(const Geometry::PropertySet* properties,
                             const std::string_view deletedName,
                             const std::size_t count,
                             std::vector<std::uint32_t>& outRows,
                             bool& outAllLive)
        {
            outRows.clear();
            outAllLive = true;
            const auto deleted = properties != nullptr
                ? properties->Get<bool>(deletedName)
                : decltype(properties->Get<bool>(deletedName)){};
            if (!deleted)
                return;
            const std::vector<bool>& flags = deleted.Vector();
            for (std::size_t row = 0u; row < count; ++row)
            {
                if (row < flags.size() && flags[row])
                    outAllLive = false;
            }
            if (outAllLive)
                return;
            outRows.reserve(count);
            for (std::size_t row = 0u; row < count; ++row)
            {
                if (row >= flags.size() || !flags[row])
                    outRows.push_back(static_cast<std::uint32_t>(row));
            }
        }
    }

    // Helpers are templates over the cache records because
    // `RenderExtractionCache::State` is private to the cache class.
    namespace
    {

        struct AnchorSources
        {
            const Geometry::PropertySet* Elements{nullptr};
            const Geometry::PropertySet* Positions{nullptr};
            std::string PositionName{};
            std::vector<Geometry::PropertyRevision> Revisions{};
            std::vector<std::size_t> Counts{};
        };

        // Collects everything the anchors of `domain` depend on. Any source
        // change moves at least one revision (revisions are global and never
        // reused), so equal revisions and counts mean an unchanged cache.
        [[nodiscard]] AnchorSources DescribeAnchorSources(
            const GeometryEntityAvailability& availability,
            const GeometryElementDomain domain)
        {
            AnchorSources sources{};
            sources.Elements = ResolveGeometryPropertySet(availability, domain);
            sources.Positions =
                ResolveGeometryPropertySet(availability, IsPointDomain(domain)
                    ? domain
                    : PositionDomainFor(domain));
            sources.PositionName = std::string{PN::kPosition};

            sources.Revisions.push_back(RevisionOf(sources.Positions, sources.PositionName));
            sources.Counts.push_back(sources.Positions != nullptr ? sources.Positions->Size() : 0u);
            sources.Counts.push_back(sources.Elements != nullptr ? sources.Elements->Size() : 0u);
            switch (domain)
            {
            case GeometryElementDomain::MeshEdge:
            case GeometryElementDomain::GraphEdge:
                sources.Revisions.push_back(RevisionOf(sources.Elements, PN::kEdgeV0));
                sources.Revisions.push_back(RevisionOf(sources.Elements, PN::kEdgeV1));
                sources.Revisions.push_back(RevisionOf(sources.Elements, "e:deleted"));
                break;
            case GeometryElementDomain::MeshFace:
            {
                const Geometry::PropertySet* halfedges =
                    availability.SourceView.HalfedgeSource != nullptr
                        ? &availability.SourceView.HalfedgeSource->Properties
                        : nullptr;
                sources.Revisions.push_back(RevisionOf(halfedges, PN::kHalfedgeToVertex));
                sources.Revisions.push_back(RevisionOf(halfedges, PN::kHalfedgeNext));
                sources.Revisions.push_back(RevisionOf(halfedges, PN::kHalfedgeFace));
                sources.Revisions.push_back(RevisionOf(sources.Elements, PN::kFaceHalfedge));
                sources.Revisions.push_back(RevisionOf(sources.Elements, "f:deleted"));
                sources.Counts.push_back(halfedges != nullptr ? halfedges->Size() : 0u);
                break;
            }
            default:
                sources.Revisions.push_back(RevisionOf(sources.Elements, "v:deleted"));
                break;
            }
            return sources;
        }

        // Rebuilds anchors and live rows. Returns false when the domain has
        // no usable anchor source (the layer then reports unavailable).
        template <typename AnchorCache>
        [[nodiscard]] bool BuildAnchorCache(
            const GeometryEntityAvailability& availability,
            const AnchorSources& sources,
            AnchorCache& cache,
            RuntimeRenderExtractionStats& stats)
        {
            cache.BorrowPositions = false;
            cache.Anchors.clear();
            cache.LiveRows.clear();
            cache.AllLive = true;
            if (sources.Elements == nullptr || sources.Positions == nullptr)
                return false;
            const auto positions = sources.Positions->Get<glm::vec3>(sources.PositionName);
            if (!positions)
                return false;
            const std::span<const glm::vec3> positionValues = positions.Span();
            const std::size_t count = sources.Elements->Size();
            stats.VectorFieldCacheScannedElementCount += count;

            if (IsPointDomain(cache.Domain))
            {
                if (positionValues.size() != count)
                    return false;
                CollectLiveRows(sources.Elements, "v:deleted", count, cache.LiveRows, cache.AllLive);
                bool finite = true;
                for (const glm::vec3& position : positionValues)
                    finite = finite && IsFinite(position);
                if (finite)
                {
                    cache.BorrowPositions = true;
                    return true;
                }
                // Non-finite anchors are replaced by zero and their rows dropped.
                cache.Anchors.assign(positionValues.begin(), positionValues.end());
                std::vector<bool> notDeleted(count, cache.AllLive);
                for (const std::uint32_t row : cache.LiveRows)
                    notDeleted[row] = true;
                std::vector<std::uint32_t> live{};
                live.reserve(count);
                for (std::size_t row = 0u; row < count; ++row)
                {
                    if (!IsFinite(cache.Anchors[row]))
                        cache.Anchors[row] = glm::vec3{0.0f};
                    else if (notDeleted[row])
                        live.push_back(static_cast<std::uint32_t>(row));
                }
                cache.LiveRows = std::move(live);
                cache.AllLive = false;
                return true;
            }

            if (cache.Domain == GeometryElementDomain::MeshFace)
            {
                const MeshSurfaceTopologyStatus status = BuildMeshFaceCenters(
                    availability.SourceView, positionValues, cache.Anchors, cache.LiveRows);
                if (status != MeshSurfaceTopologyStatus::Success ||
                    cache.Anchors.size() != count)
                {
                    cache.Anchors.clear();
                    cache.LiveRows.clear();
                    return false;
                }
                cache.AllLive = cache.LiveRows.size() == count;
                if (cache.AllLive)
                    cache.LiveRows.clear();
                return true;
            }

            // Edge midpoints from the canonical endpoint rows.
            const auto v0 = sources.Elements->Get<std::uint32_t>(PN::kEdgeV0);
            const auto v1 = sources.Elements->Get<std::uint32_t>(PN::kEdgeV1);
            if (!v0 || !v1 || v0.Vector().size() != count || v1.Vector().size() != count)
                return false;
            std::vector<std::uint32_t> notDeleted{};
            bool allNotDeleted = true;
            CollectLiveRows(sources.Elements, "e:deleted", count, notDeleted, allNotDeleted);
            cache.Anchors.assign(count, glm::vec3{0.0f});
            cache.LiveRows.reserve(count);
            std::size_t deletedCursor = 0u;
            for (std::size_t edge = 0u; edge < count; ++edge)
            {
                bool rowLive = allNotDeleted;
                if (!allNotDeleted)
                {
                    while (deletedCursor < notDeleted.size() && notDeleted[deletedCursor] < edge)
                        ++deletedCursor;
                    rowLive = deletedCursor < notDeleted.size() && notDeleted[deletedCursor] == edge;
                }
                const std::uint32_t a = v0.Vector()[edge];
                const std::uint32_t b = v1.Vector()[edge];
                if (!rowLive || a >= positionValues.size() || b >= positionValues.size())
                    continue;
                // Averaged in double so finite coordinates near the float
                // limit cannot overflow.
                const glm::vec3 midpoint{
                    0.5 * (glm::dvec3{positionValues[a]} + glm::dvec3{positionValues[b]})};
                if (!IsFinite(midpoint))
                    continue;
                cache.Anchors[edge] = midpoint;
                cache.LiveRows.push_back(static_cast<std::uint32_t>(edge));
            }
            cache.AllLive = cache.LiveRows.size() == count;
            if (cache.AllLive)
                cache.LiveRows.clear();
            return true;
        }

        template <typename EntityCache>
        [[nodiscard]] typename decltype(EntityCache::Anchors)::value_type* EnsureAnchorCache(
            EntityCache& entityCache,
            const GeometryEntityAvailability& availability,
            const GeometryElementDomain domain,
            const std::uint64_t frame,
            RuntimeRenderExtractionStats& stats)
        {
            using AnchorCache = typename decltype(entityCache.Anchors)::value_type;
            AnchorSources sources = DescribeAnchorSources(availability, domain);
            AnchorCache* cache = nullptr;
            for (AnchorCache& candidate : entityCache.Anchors)
            {
                if (candidate.Domain == domain)
                    cache = &candidate;
            }
            if (cache != nullptr &&
                cache->Revisions == sources.Revisions && cache->Counts == sources.Counts)
            {
                if (cache->LastUsedFrame != frame)
                    ++stats.VectorFieldAnchorCacheReuses;
                cache->LastUsedFrame = frame;
                // Stamp 0 remembers a failed build of these same sources.
                return cache->Stamp != 0u ? cache : nullptr;
            }
            if (cache == nullptr)
            {
                entityCache.Anchors.push_back(AnchorCache{.Domain = domain});
                cache = &entityCache.Anchors.back();
            }
            cache->LastUsedFrame = frame;
            cache->Revisions = std::move(sources.Revisions);
            cache->Counts = std::move(sources.Counts);
            ++stats.VectorFieldAnchorCacheBuilds;
            if (!BuildAnchorCache(availability, sources, *cache, stats))
            {
                // A failed build keeps Stamp 0 so an unchanged source is not
                // rescanned every frame.
                cache->Stamp = 0u;
                return nullptr;
            }
            cache->Stamp = NextVectorFieldCacheStamp();
            return cache;
        }

        template <typename EntityCache>
        [[nodiscard]] typename decltype(EntityCache::Payloads)::value_type* EnsurePayloadCache(
            EntityCache& entityCache,
            const Geometry::PropertySet& properties,
            const GeometryElementDomain domain,
            const std::string& property,
            const std::uint64_t frame,
            RuntimeRenderExtractionStats& stats)
        {
            const Geometry::PropertyRevision revision = RevisionOf(&properties, property);
            using PayloadCache = typename decltype(entityCache.Payloads)::value_type;
            const std::size_t count = properties.Size();
            PayloadCache* cache = nullptr;
            for (PayloadCache& candidate : entityCache.Payloads)
            {
                if (candidate.Domain == domain && candidate.Property == property)
                    cache = &candidate;
            }
            if (cache != nullptr && revision != 0u &&
                cache->Revision == revision && cache->Count == count)
            {
                if (cache->LastUsedFrame != frame)
                    ++stats.VectorFieldPayloadCacheReuses;
                cache->LastUsedFrame = frame;
                return cache->Stamp != 0u ? cache : nullptr;
            }
            if (cache == nullptr)
            {
                entityCache.Payloads.push_back(PayloadCache{
                    .Domain = domain,
                    .Property = property,
                });
                cache = &entityCache.Payloads.back();
            }
            cache->LastUsedFrame = frame;
            cache->Revision = revision;
            cache->Count = count;
            cache->Sanitized.clear();
            cache->NonFiniteCount = 0u;
            ++stats.VectorFieldPayloadCacheBuilds;

            const auto values = properties.Get<glm::vec3>(property);
            if (!values || values.Span().size() != count)
            {
                cache->Stamp = 0u;
                return nullptr;
            }
            const std::span<const glm::vec3> span = values.Span();
            stats.VectorFieldCacheScannedElementCount += span.size();
            for (const glm::vec3& value : span)
            {
                if (!IsFinite(value))
                    ++cache->NonFiniteCount;
            }
            cache->Borrow = cache->NonFiniteCount == 0u;
            if (!cache->Borrow)
            {
                // Non-finite vectors become zero, which draws no glyph.
                cache->Sanitized.assign(span.begin(), span.end());
                for (glm::vec3& value : cache->Sanitized)
                {
                    if (!IsFinite(value))
                        value = glm::vec3{0.0f};
                }
            }
            cache->Stamp = NextVectorFieldCacheStamp();
            return cache;
        }
    }

    void RenderExtractionCache::State::AppendVectorFieldLayers(
        entt::registry& registry,
        const entt::entity entity,
        const std::uint32_t stableId,
        const glm::mat4& worldMatrix,
        RuntimeRenderExtractionStats& stats)
    {
        std::vector<VectorFieldRequest> requests{};
        if (const auto* recipe = registry.try_get<GeometryPresentationRecipe>(entity))
        {
            for (const GeometryVectorFieldLayerRecipe& layer : recipe->VectorFields)
            {
                if (!layer.Enabled)
                    continue;
                requests.push_back(VectorFieldRequest{
                    .Domain = layer.Vector.Domain,
                    .Property = layer.Vector.Name,
                    .Name = layer.Vector.Name,
                    .Scale = layer.Length,
                    .Normalize = layer.LengthMode == GeometryVectorFieldLengthMode::Normalized,
                    .LineWidthPx = layer.LineWidthPx,
                    .Color = layer.Color,
                    .DepthTested = layer.DepthTested,
                    .Stride = layer.Stride,
                    .MaxGlyphs = layer.MaxGlyphs,
                });
            }
        }
        if (requests.empty())
            return;

        const std::uint64_t frame = m_VectorFieldFrame;
        VectorFieldEntityCache& entityCache = m_VectorFieldCaches[stableId];
        entityCache.LastUsedFrame = frame;
        const GeometryEntityAvailability availability = BuildGeometryAvailability(registry, entity);

        for (const VectorFieldRequest& request : requests)
        {
            ++stats.VectorFieldLayerCount;
            if (!SupportsGeometryVectorFieldDomain(request.Domain))
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }
            const GeometryPropertyResolution resolution = ResolveGeometryProperty(
                availability,
                request.Domain,
                request.Property,
                Geometry::PropertyValueKind::Vec3,
                ResolveGeometryElementCount(availability, request.Domain));
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, request.Domain);
            if (!resolution.Resolved() || properties == nullptr ||
                properties->Size() == 0u ||
                properties->Size() > std::numeric_limits<std::uint32_t>::max())
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }

            const VectorFieldAnchorCache* anchors = EnsureAnchorCache(
                entityCache, availability, request.Domain, frame, stats);
            const VectorFieldPayloadCache* payload = EnsurePayloadCache(
                entityCache, *properties, request.Domain, request.Property, frame, stats);
            if (anchors == nullptr || payload == nullptr)
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }
            stats.VectorFieldNonFiniteVectorCount += payload->NonFiniteCount;

            // Point domains borrow their own canonical positions.
            const std::span<const glm::vec3> anchorValues = anchors->BorrowPositions
                ? properties->Get<glm::vec3>(PN::kPosition).Span()
                : std::span<const glm::vec3>{anchors->Anchors};
            const std::span<const glm::vec3> vectorValues = payload->Borrow
                ? properties->Get<glm::vec3>(request.Property).Span()
                : std::span<const glm::vec3>{payload->Sanitized};

            VisualizationEncodingDiagnostics appendDiagnostics{};
            const VisualizationRecipeStatus status = AppendVectorFieldPacket(
                m_VisualizationState.Batch,
                VectorFieldPacketInputs{
                    .Name = BuildKey(stableId, "vector", request.Domain, request.Name),
                    .Domain = ToAttributeDomain(request.Domain),
                    .ElementCount = static_cast<std::uint32_t>(properties->Size()),
                    .AnchorKey = BuildKey(stableId, "vector.anchor", request.Domain, {}),
                    .Anchors = anchorValues,
                    .AnchorStamp = anchors->Stamp,
                    .VectorKey = BuildKey(stableId, "vector.value", request.Domain, request.Property),
                    .Vectors = vectorValues,
                    .VectorStamp = payload->Stamp,
                    .RowKey = anchors->AllLive
                        ? std::string{}
                        : BuildKey(stableId, "vector.rows", request.Domain, {}),
                    .Rows = anchors->AllLive
                        ? std::span<const std::uint32_t>{}
                        : std::span<const std::uint32_t>{anchors->LiveRows},
                    .RowStamp = anchors->Stamp,
                    // Cache rows are produced from the element range at build.
                    .RowsPrevalidated = true,
                    .Stride = request.Stride,
                    .MaxGlyphs = request.MaxGlyphs,
                    .ObjectToWorld = worldMatrix,
                    .Scale = request.Scale,
                    .NormalizeLength = request.Normalize,
                    .LineWidthPx = request.LineWidthPx,
                    .Color = request.Color,
                    .DepthTested = request.DepthTested,
                    .CopyPayloads = false,
                },
                &appendDiagnostics);
            stats.VectorFieldRowIndexCheckCount += appendDiagnostics.VectorRowIndexCheckCount;
            if (status == VisualizationRecipeStatus::Encoded)
                ++stats.VectorFieldPacketCount;
            else
                ++stats.VectorFieldUnavailableCount;
        }
    }

    void RenderExtractionCache::State::ReleaseUnusedVectorFieldCaches(
        RuntimeRenderExtractionStats& stats)
    {
        const std::uint64_t frame = m_VectorFieldFrame;
        for (auto it = m_VectorFieldCaches.begin(); it != m_VectorFieldCaches.end();)
        {
            VectorFieldEntityCache& cache = it->second;
            if (cache.LastUsedFrame != frame)
            {
                stats.VectorFieldCacheReleases += static_cast<std::uint32_t>(
                    cache.Anchors.size() + cache.Payloads.size());
                it = m_VectorFieldCaches.erase(it);
                continue;
            }
            std::erase_if(cache.Anchors, [&](const VectorFieldAnchorCache& anchor) {
                const bool unused = anchor.LastUsedFrame != frame;
                stats.VectorFieldCacheReleases += unused ? 1u : 0u;
                return unused;
            });
            std::erase_if(cache.Payloads, [&](const VectorFieldPayloadCache& payload) {
                const bool unused = payload.LastUsedFrame != frame;
                stats.VectorFieldCacheReleases += unused ? 1u : 0u;
                return unused;
            });
            ++it;
        }
        ++m_VectorFieldFrame;
    }
}

// Extracts vector-field overlay packets from Appearance layers, with
// per-entity anchor and payload caches keyed by source property revisions.
// Explicit `VectorFieldVisualizationRecipe`s keep the copying recipe encoder.
module;

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
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

import Geometry.Validation;
import :Internal;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Extrinsic.Runtime.VisualizationRecipes;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Geometry::Validation::IsFinite;

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

        // Rows of a glyph order a layer draws (0: all).
        [[nodiscard]] std::uint32_t OrderedDrawRowCount(
            const std::size_t rows, const std::uint32_t stride, const std::uint32_t maxGlyphs) noexcept
        {
            const std::size_t strided = stride > 1u ? (rows + stride - 1u) / stride : rows;
            const std::size_t count = maxGlyphs > 0u ? std::min<std::size_t>(strided, maxGlyphs) : strided;
            return count >= rows ? 0u : static_cast<std::uint32_t>(count);
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

        // UI-083: anchors sit on the primary lane's displayed positions.
        [[nodiscard]] GeometryRenderLane AnchorLane(const GeometryEntityAvailability& availability) noexcept
        {
            return PrimaryLaneFor(availability.Sources.ProvenanceDomain, RenderAttribute::Position)
                .value_or(GeometryRenderLane::Points);
        }

        // Collects everything the anchors of `domain` depend on. Any source
        // change moves at least one revision (revisions are global and never
        // reused), so equal revisions and counts mean an unchanged cache.
        [[nodiscard]] AnchorSources DescribeAnchorSources(
            const GeometryEntityAvailability& availability,
            const GeometryElementDomain domain,
            const VertexChannelBindingSet* bindings)
        {
            AnchorSources sources{};
            sources.Elements = ResolveGeometryPropertySet(availability, domain);
            const GeometryElementDomain positionDomain =
                IsPointDomain(domain) ? domain : PositionDomainFor(domain);
            sources.Positions = ResolveGeometryPropertySet(availability, positionDomain);
            // Anchors sit on the displayed positions (RUNTIME-315).
            const GeometryRenderLane lane = AnchorLane(availability);
            sources.PositionName = sources.Positions != nullptr
                ? std::string{ResolveDisplayedPositions(*sources.Positions, positionDomain, bindings, lane).Name}
                : std::string{PN::kPosition};

            sources.Revisions.push_back(RevisionOf(sources.Positions, sources.PositionName));
            sources.Revisions.push_back(std::hash<std::string>{}(sources.PositionName));
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
            const VertexChannelBindingSet* bindings,
            const std::uint64_t frame,
            RuntimeRenderExtractionStats& stats)
        {
            using AnchorCache = typename decltype(entityCache.Anchors)::value_type;
            AnchorSources sources = DescribeAnchorSources(availability, domain, bindings);
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

        // UI-083: the converted values of one length/color property; nullptr when it is missing,
        // has another count or is neither scalar nor vec3/vec4. A cache used earlier this frame is
        // reused as is: its payload is already lent to the batch. The returned entry is valid until
        // the next call (a new entry may move it; lent spans point into its own buffers and stay).
        template <typename EntityCache>
        [[nodiscard]] const typename decltype(EntityCache::Styles)::value_type* EnsureStyleCache(
            EntityCache& entityCache,
            const Geometry::PropertySet& properties,
            const GeometryElementDomain domain,
            const std::string& property,
            const std::uint64_t frame,
            RuntimeRenderExtractionStats& stats)
        {
            using StyleCache = typename decltype(entityCache.Styles)::value_type;
            using Kind = typename StyleCache::ValueKind;
            const Geometry::PropertyRevision revision = RevisionOf(&properties, property);
            const std::size_t count = properties.Size();
            StyleCache* cache = nullptr;
            for (StyleCache& candidate : entityCache.Styles)
            {
                if (candidate.Domain == domain && candidate.Property == property)
                    cache = &candidate;
            }
            if (cache != nullptr && cache->Count == count &&
                (cache->LastUsedFrame == frame || (revision != 0u && cache->Revision == revision)))
            {
                if (cache->LastUsedFrame != frame)
                    ++stats.VectorFieldPayloadCacheReuses;
                cache->LastUsedFrame = frame;
                return cache->Kind != Kind::None ? cache : nullptr;
            }
            if (cache == nullptr)
            {
                entityCache.Styles.push_back(StyleCache{.Domain = domain, .Property = property});
                cache = &entityCache.Styles.back();
            }
            cache->LastUsedFrame = frame;
            cache->Revision = revision;
            cache->Count = count;
            cache->Kind = Kind::None;
            cache->Scalars.clear();
            cache->Rgba.clear();
            cache->AutoRangeMin = 0.0f;
            cache->AutoRangeMax = 1.0f;
            ++stats.VectorFieldPayloadCacheBuilds;
            stats.VectorFieldCacheScannedElementCount += count;

            if (ConvertVisualizationScalars(properties, property, cache->Scalars))
            {
                if (cache->Scalars.size() != count)
                    return nullptr;
                // Invalid values are +inf and stay out of the range.
                (void)ComputeVisualizationAutoRange(cache->Scalars, cache->AutoRangeMin, cache->AutoRangeMax);
                cache->Kind = Kind::Scalar;
            }
            else
            {
                const auto toRgba = [&](const auto& values, const auto& widen)
                {
                    if (values.size() != count)
                        return;
                    cache->Rgba.reserve(count);
                    for (const auto& value : values)
                    {
                        const glm::vec4 rgba = widen(value);
                        const bool finite = IsFinite(glm::vec3{rgba}) && std::isfinite(rgba.w);
                        cache->Rgba.push_back(finite ? glm::clamp(rgba, 0.0f, 1.0f) : glm::vec4{-1.0f});
                    }
                    cache->Kind = Kind::Rgba;
                };
                if (const auto rgb = properties.Get<glm::vec3>(property))
                    toRgba(rgb.Span(),
                        [](const glm::vec3& v)
                        {
                            return glm::vec4{v, 1.0f};
                        });
                else if (const auto rgba = properties.Get<glm::vec4>(property))
                    toRgba(rgba.Span(),
                        [](const glm::vec4& v)
                        {
                            return v;
                        });
                if (cache->Kind == Kind::None)
                    return nullptr;
            }
            cache->Stamp = NextVectorFieldCacheStamp();
            return cache;
        }
    }

    namespace
    {
        // The domain's glyph order (UI-081) restricted to the anchor cache's live rows; nullptr when
        // the domain has no valid order (the layer falls back to its stride).
        template <typename EntityCache>
        [[nodiscard]] const typename decltype(EntityCache::Orders)::value_type* EnsureOrderCache(
            EntityCache& entityCache,
            const Geometry::PropertySet& properties,
            const GeometryElementDomain domain,
            const std::uint64_t anchorStamp,
            const std::span<const std::uint32_t> liveRows,
            const bool allLive,
            const std::uint64_t frame)
        {
            using OrderCache = typename decltype(entityCache.Orders)::value_type;
            // No rank property, no cache: a domain without an order costs nothing.
            if (!properties.Exists(GeometryGlyphOrderPropertyName(domain)))
                return nullptr;
            // The anchor stamp already moves with the element count and the deletion mask.
            const Geometry::PropertyRevision revision = RevisionOf(&properties, GeometryGlyphOrderPropertyName(domain));
            OrderCache* cache = nullptr;
            for (OrderCache& candidate : entityCache.Orders)
            {
                if (candidate.Domain == domain)
                    cache = &candidate;
            }
            // Revision 0 is an unmodified or absent property: no identity to key on, so rebuild.
            if (cache != nullptr && revision != 0u && cache->Revision == revision && cache->AnchorStamp == anchorStamp)
            {
                cache->LastUsedFrame = frame;
                return cache->Rows.empty() ? nullptr : cache;
            }
            if (cache == nullptr)
            {
                entityCache.Orders.push_back(OrderCache{.Domain = domain});
                cache = &entityCache.Orders.back();
            }
            cache->Revision = revision;
            cache->AnchorStamp = anchorStamp;
            cache->LastUsedFrame = frame;
            cache->Rows.clear();
            if (std::optional<std::vector<std::uint32_t>> sorted = SortGeometryGlyphOrderRows(properties, domain))
            {
                cache->Rows = std::move(*sorted);
                // Anchors without a finite position (face centers of degenerate faces) draw nothing.
                if (!allLive)
                {
                    std::vector<bool> drawable(properties.Size(), false);
                    for (const std::uint32_t row : liveRows)
                        drawable[row] = true;
                    std::erase_if(cache->Rows,
                        [&](const std::uint32_t row)
                        {
                            return !drawable[row];
                        });
                }
            }
            cache->Stamp = NextVectorFieldCacheStamp();
            return cache->Rows.empty() ? nullptr : cache;
        }
    }

    namespace
    {
        [[nodiscard]] bool IsScalarKind(const Geometry::PropertyValueKind kind) noexcept
        {
            using K = Geometry::PropertyValueKind;
            return kind == K::Bool || kind == K::Int32 || kind == K::UInt32 || kind == K::UInt64 || kind == K::Float ||
                kind == K::Double;
        }

        // UI-083: binds a layer's length and color properties, a GPU preview front by address
        // (geometry.property-coherence), else the CPU style cache. A property that resolves
        // neither way leaves the uniform length or color and is counted.
        template <typename EntityCache, typename ObserveFront>
        void BindVectorFieldStyle(const ObserveFront& observeFront,
            const std::uint32_t stableId,
            const GeometryVectorFieldLayerRecipe& layer,
            const Geometry::PropertySet& properties,
            EntityCache& entityCache,
            const std::uint64_t frame,
            RuntimeRenderExtractionStats& stats,
            VectorFieldPacketInputs& inputs)
        {
            using Kind = typename decltype(entityCache.Styles)::value_type::ValueKind;
            const auto styleCache = [&](const std::string& name)
            {
                return EnsureStyleCache(entityCache, properties, layer.Vector.Domain, name, frame, stats);
            };
            const auto scalarKey = [&](const std::string& name)
            {
                return BuildKey(stableId, "vector.scalar", layer.Vector.Domain, name);
            };

            if (!layer.LengthProperty.empty())
            {
                if (const auto front = observeFront(layer.LengthProperty))
                {
                    inputs.LengthBDA = front->Address;
                    inputs.LengthStamp = front->Stamp;
                }
                else if (const auto* cache = styleCache(layer.LengthProperty); cache && cache->Kind == Kind::Scalar)
                {
                    inputs.LengthKey = scalarKey(layer.LengthProperty);
                    inputs.Lengths = cache->Scalars;
                    inputs.LengthStamp = cache->Stamp;
                }
                else
                {
                    ++stats.VectorFieldStyleFallbackCount;
                }
            }

            if (layer.ColorProperty.empty())
                return;
            inputs.Style.ColorMap = layer.ColorMap;
            inputs.Style.ColorRangeMin = layer.ColorRangeMin;
            inputs.Style.ColorRangeMax = layer.ColorRangeMax;
            std::optional<std::array<float, 2>> autoRange{};
            if (const auto front = observeFront(layer.ColorProperty))
            {
                inputs.ColorMode = Graphics::VectorFieldColorMode::Colormap;
                inputs.ColorBDA = front->Address;
                inputs.ColorStamp = front->Stamp;
                if (front->ScalarRange)
                {
                    autoRange = WidenGpuFrontScalarRange(*front->ScalarRange);
                }
                else if (layer.ColorAutoRange)
                {
                    // An existing property keeps its CPU auto range; a GPU-only one draws 0..1.
                    if (const auto* cache = styleCache(layer.ColorProperty); cache && cache->Kind == Kind::Scalar)
                        autoRange = std::array{cache->AutoRangeMin, cache->AutoRangeMax};
                    else
                        autoRange = std::array{0.0f, 1.0f};
                }
            }
            else if (const auto* cache = styleCache(layer.ColorProperty); cache != nullptr)
            {
                inputs.ColorStamp = cache->Stamp;
                if (cache->Kind == Kind::Scalar)
                {
                    inputs.ColorMode = Graphics::VectorFieldColorMode::Colormap;
                    inputs.ColorKey = scalarKey(layer.ColorProperty);
                    inputs.ColorScalars = cache->Scalars;
                    autoRange = std::array{cache->AutoRangeMin, cache->AutoRangeMax};
                }
                else
                {
                    inputs.ColorMode = Graphics::VectorFieldColorMode::Rgba;
                    inputs.ColorKey = BuildKey(stableId, "vector.rgba", layer.Vector.Domain, layer.ColorProperty);
                    inputs.ColorRgba = cache->Rgba;
                }
            }
            else
            {
                ++stats.VectorFieldStyleFallbackCount;
                return;
            }
            if (layer.ColorAutoRange && autoRange && std::isfinite((*autoRange)[0]) &&
                std::isfinite((*autoRange)[1]) && (*autoRange)[0] < (*autoRange)[1])
            {
                inputs.Style.ColorRangeMin = (*autoRange)[0];
                inputs.Style.ColorRangeMax = (*autoRange)[1];
            }
            else if (layer.ColorAutoRange)
            {
                inputs.Style.ColorRangeMin = 0.0f;
                inputs.Style.ColorRangeMax = 1.0f;
            }
        }
    }

    void RenderExtractionCache::State::AppendVectorFieldLayers(
        entt::registry& registry,
        const entt::entity entity,
        const std::uint32_t stableId,
        const glm::mat4& worldMatrix,
        RuntimeRenderExtractionStats& stats)
    {
        // Copied: building packets calls out (the GPU front observer) while the recipe is in the registry.
        std::vector<GeometryVectorFieldLayerRecipe> layers{};
        if (const auto* recipe = registry.try_get<GeometryPresentationRecipe>(entity))
        {
            for (const GeometryVectorFieldLayerRecipe& layer : recipe->VectorFields)
            {
                if (layer.Enabled)
                    layers.push_back(layer);
            }
        }
        if (layers.empty())
            return;

        const std::uint64_t frame = m_VectorFieldFrame;
        VectorFieldEntityCache& entityCache = m_VectorFieldCaches[stableId];
        entityCache.LastUsedFrame = frame;
        const GeometryEntityAvailability availability = BuildGeometryAvailability(registry, entity);
        const auto* bindings = registry.try_get<VertexChannelBindingSet>(entity);

        for (const GeometryVectorFieldLayerRecipe& layer : layers)
        {
            ++stats.VectorFieldLayerCount;
            if (!SupportsGeometryVectorFieldDomain(layer.Vector.Domain))
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }
            const GeometryPropertyResolution resolution = ResolveGeometryProperty(
                availability,
                layer.Vector.Domain,
                layer.Vector.Name,
                Geometry::PropertyValueKind::Vec3,
                ResolveGeometryElementCount(availability, layer.Vector.Domain));
            const Geometry::PropertySet* properties =
                ResolveGeometryPropertySet(availability, layer.Vector.Domain);
            if (!resolution.Resolved() || properties == nullptr ||
                properties->Size() == 0u ||
                properties->Size() > std::numeric_limits<std::uint32_t>::max())
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }

            const VectorFieldAnchorCache* anchors = EnsureAnchorCache(
                entityCache, availability, layer.Vector.Domain, bindings, frame, stats);
            const VectorFieldPayloadCache* payload = EnsurePayloadCache(
                entityCache, *properties, layer.Vector.Domain, layer.Vector.Name, frame, stats);
            if (anchors == nullptr || payload == nullptr)
            {
                ++stats.VectorFieldUnavailableCount;
                continue;
            }
            stats.VectorFieldNonFiniteVectorCount += payload->NonFiniteCount;

            // Point domains borrow their own displayed positions.
            const std::span<const glm::vec3> anchorValues = anchors->BorrowPositions
                ? ResolveDisplayedPositions(*properties, layer.Vector.Domain, bindings, AnchorLane(availability)).Values
                : std::span<const glm::vec3>{anchors->Anchors};
            const std::span<const glm::vec3> vectorValues = payload->Borrow
                ? properties->Get<glm::vec3>(layer.Vector.Name).Span()
                : std::span<const glm::vec3>{payload->Sanitized};

            // A valid glyph order draws a prefix instead of every n-th row: as many rows as the
            // stride would (ceil(L / Stride), so older layers keep their count), at most MaxGlyphs.
            const auto* order = EnsureOrderCache(
                entityCache, *properties, layer.Vector.Domain, anchors->Stamp, anchors->LiveRows, anchors->AllLive, frame);
            const bool ordered = order != nullptr;
            if (ordered)
                ++stats.VectorFieldOrderedLayerCount;

            VectorFieldPacketInputs inputs{
                .Name = BuildKey(stableId, "vector", layer.Vector.Domain, layer.Vector.Name),
                .Domain = ToAttributeDomain(layer.Vector.Domain),
                .ElementCount = static_cast<std::uint32_t>(properties->Size()),
                .AnchorKey = BuildKey(stableId, "vector.anchor", layer.Vector.Domain, {}),
                .Anchors = anchorValues,
                .AnchorStamp = anchors->Stamp,
                .VectorKey = BuildKey(stableId, "vector.value", layer.Vector.Domain, layer.Vector.Name),
                .Vectors = vectorValues,
                .VectorStamp = payload->Stamp,
                .RowKey = ordered      ? BuildKey(stableId, "vector.order", layer.Vector.Domain, {})
                    : anchors->AllLive ? std::string{}
                                       : BuildKey(stableId, "vector.rows", layer.Vector.Domain, {}),
                .Rows = ordered        ? std::span<const std::uint32_t>{order->Rows}
                    : anchors->AllLive ? std::span<const std::uint32_t>{}
                                       : std::span<const std::uint32_t>{anchors->LiveRows},
                .RowStamp = ordered ? order->Stamp : anchors->Stamp,
                    // Cache rows are produced from the element range at build.
                .RowsPrevalidated = true,
                .Stride = ordered ? 1u : layer.Stride,
                .MaxGlyphs = ordered ? 0u : layer.MaxGlyphs,
                .DrawRowCount =
                    ordered ? OrderedDrawRowCount(order->Rows.size(), layer.Stride, layer.MaxGlyphs) : 0u,
                .ObjectToWorld = worldMatrix,
                .Style = {.Scale = layer.Length,
                          .NormalizeLength = layer.LengthMode == GeometryVectorFieldLengthMode::Normalized,
                          .LineWidthPx = layer.LineWidthPx,
                          .Color = layer.Color,
                          .DepthTested = layer.DepthTested},
                .CopyPayloads = false,
            };
            const auto observeFront =
                [&](const std::string& name) -> std::optional<RenderExtractionCache::GpuPropertyFront>
            {
                if (!m_GpuPropertyObserver)
                    return std::nullopt;
                // A name the CPU does not have yet is a pending float output; vec3/vec4 colors
                // have no scalar fronts.
                Geometry::PropertyValueKind kind = DetectGeometryPropertyValueKind(*properties, name);
                if (kind == Geometry::PropertyValueKind::Unknown)
                    kind = Geometry::PropertyValueKind::Float;
                else if (!IsScalarKind(kind))
                    return std::nullopt;
                auto front = m_GpuPropertyObserver(m_World, entity, GeometryPropertyRef{layer.Vector.Domain, name, kind});
                if (!front || front->Address == 0u || front->Count != properties->Size())
                    return std::nullopt;
                return front;
            };
            BindVectorFieldStyle(observeFront, stableId, layer, *properties, entityCache, frame, stats, inputs);

            VisualizationEncodingDiagnostics appendDiagnostics{};
            const VisualizationRecipeStatus status =
                AppendVectorFieldPacket(m_VisualizationState.Batch, inputs, &appendDiagnostics);
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
                    cache.Anchors.size() + cache.Payloads.size() + cache.Orders.size() + cache.Styles.size());
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
            std::erase_if(cache.Orders,
                [&](const VectorFieldOrderCache& order)
                {
                    const bool unused = order.LastUsedFrame != frame;
                    stats.VectorFieldCacheReleases += unused ? 1u : 0u;
                    return unused;
                });
            std::erase_if(cache.Styles,
                [&](const VectorFieldStyleCache& style)
                {
                    const bool unused = style.LastUsedFrame != frame;
                    stats.VectorFieldCacheReleases += unused ? 1u : 0u;
                    return unused;
                });
            ++it;
        }
        ++m_VectorFieldFrame;
    }
}

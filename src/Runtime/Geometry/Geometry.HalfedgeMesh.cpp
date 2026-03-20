module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>
#include <span>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

module Geometry.HalfedgeMesh;

import Geometry.Properties;

namespace Geometry::Halfedge
{
    void Mesh::SetVertexAttributeTransferRules(std::span<const VertexAttributeTransfer> rules)
    {
        m_VertexAttrTransfer.assign(rules.begin(), rules.end());
    }

    void Mesh::ClearVertexAttributeTransferRules()
    {
        m_VertexAttrTransfer.clear();
    }

    namespace
    {
        [[nodiscard]] float ComputeTransferParameter(
            Vertices& verts,
            VertexHandle va,
            VertexHandle vb,
            const glm::vec3& outPosition) noexcept
        {
            const auto points = verts.Get<glm::vec3>("v:point");
            if (!points)
            {
                return 0.5f;
            }

            const glm::vec3 a = points[va.Index];
            const glm::vec3 b = points[vb.Index];
            const glm::vec3 ab = b - a;
            const float lenSq = glm::dot(ab, ab);
            if (lenSq <= 1.0e-20f)
            {
                return 0.5f;
            }

            const float t = glm::dot(outPosition - a, ab) / lenSq;
            return std::clamp(t, 0.0f, 1.0f);
        }

        template <class T>
        void TransferRule_T(
            Vertices& verts,
            const Mesh::VertexAttributeTransfer& rule,
            VertexHandle va,
            VertexHandle vb,
            VertexHandle vOut,
            float t)
        {
            auto prop = verts.Get<T>(rule.Name);
            if (!prop) return;

            switch (rule.Rule)
            {
            case Mesh::VertexAttributeTransfer::Policy::Average:
                prop[vOut.Index] = (static_cast<T>(1.0f - t) * prop[va.Index])
                                 + (static_cast<T>(t) * prop[vb.Index]);
                break;
            case Mesh::VertexAttributeTransfer::Policy::KeepA:
                prop[vOut.Index] = prop[va.Index];
                break;
            case Mesh::VertexAttributeTransfer::Policy::KeepB:
                prop[vOut.Index] = prop[vb.Index];
                break;
            case Mesh::VertexAttributeTransfer::Policy::None:
            default:
                break;
            }
        }

        void TransferRule_Dynamic(
            Vertices& verts,
            const Mesh::VertexAttributeTransfer& rule,
            VertexHandle va,
            VertexHandle vb,
            VertexHandle vOut,
            const glm::vec3& outPosition)
        {
            const float t = ComputeTransferParameter(verts, va, vb, outPosition);

            // For now we only support the common math POD types used in geometry.
            // This avoids RTTI/casting from PropertyRegistry while still working
            // for texcoords/normals/colors and scalar weights.
            TransferRule_T<float>(verts, rule, va, vb, vOut, t);
            TransferRule_T<glm::vec2>(verts, rule, va, vb, vOut, t);
            TransferRule_T<glm::vec3>(verts, rule, va, vb, vOut, t);
            TransferRule_T<glm::vec4>(verts, rule, va, vb, vOut, t);

            // Discrete attributes: average isn't meaningful; treat as KeepA by default.
            {
                auto prop = verts.Get<std::uint32_t>(rule.Name);
                if (prop)
                {
                    switch (rule.Rule)
                    {
                    case Mesh::VertexAttributeTransfer::Policy::KeepB:
                        prop[vOut.Index] = prop[vb.Index];
                        break;
                    case Mesh::VertexAttributeTransfer::Policy::None:
                        break;
                    case Mesh::VertexAttributeTransfer::Policy::Average:
                    case Mesh::VertexAttributeTransfer::Policy::KeepA:
                    default:
                        prop[vOut.Index] = prop[va.Index];
                        break;
                    }
                }
            }
        }
    }

    void Mesh::TransferVertexAttributes_OnSplit(VertexHandle va, VertexHandle vb, VertexHandle vm, glm::vec3 newPosition)
    {
        if (m_VertexAttrTransfer.empty()) return;
        for (const auto& rule : m_VertexAttrTransfer)
        {
            if (rule.Name.empty()) continue;
            TransferRule_Dynamic(m_Vertices, rule, va, vb, vm, newPosition);
        }
    }

    void Mesh::TransferVertexAttributes_OnCollapse(VertexHandle va, VertexHandle vb, VertexHandle vSurvivor, glm::vec3 newPosition)
    {
        if (m_VertexAttrTransfer.empty()) return;
        for (const auto& rule : m_VertexAttrTransfer)
        {
            if (rule.Name.empty()) continue;
            TransferRule_Dynamic(m_Vertices, rule, va, vb, vSurvivor, newPosition);
        }
    }

    Mesh::Mesh()
    {
        EnsureProperties();
    }

    Mesh::Mesh(const Mesh& rhs)
        : m_Vertices(rhs.m_Vertices)
        , m_Halfedges(rhs.m_Halfedges)
        , m_Edges(rhs.m_Edges)
        , m_Faces(rhs.m_Faces)
        , m_DeletedVertices(rhs.m_DeletedVertices)
        , m_DeletedEdges(rhs.m_DeletedEdges)
        , m_DeletedFaces(rhs.m_DeletedFaces)
        , m_HasGarbage(rhs.m_HasGarbage)
        , m_VertexAttrTransfer(rhs.m_VertexAttrTransfer)
    {
        // PropertyBuffer<T> holds raw pointers into PropertyRegistry storage.
        // After copying the registries, rebind the wrappers to this instance's storage.
        EnsureProperties();
    }

    Mesh::~Mesh() = default;

    Mesh& Mesh::operator=(const Mesh& rhs)
    {
        if (this != &rhs)
        {
            m_Vertices = rhs.m_Vertices;
            m_Halfedges = rhs.m_Halfedges;
            m_Edges = rhs.m_Edges;
            m_Faces = rhs.m_Faces;
            m_DeletedVertices = rhs.m_DeletedVertices;
            m_DeletedEdges = rhs.m_DeletedEdges;
            m_DeletedFaces = rhs.m_DeletedFaces;
            m_HasGarbage = rhs.m_HasGarbage;
            m_VertexAttrTransfer = rhs.m_VertexAttrTransfer;
            EnsureProperties();
        }
        return *this;
    }

    void Mesh::EnsureProperties()
    {
        // Built-in properties (match PMP naming to ease porting/debugging)
        m_VPoint = VertexProperty<glm::vec3>(m_Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f)));
        m_VConn = VertexProperty<VertexConnectivity>(m_Vertices.GetOrAdd<VertexConnectivity>("v:connectivity", {}));
        m_HConn = HalfedgeProperty<HalfedgeConnectivity>(m_Halfedges.GetOrAdd<HalfedgeConnectivity>("h:connectivity", {}));
        m_FConn = FaceProperty<FaceConnectivity>(m_Faces.GetOrAdd<FaceConnectivity>("f:connectivity", {}));

        m_VDeleted = VertexProperty<bool>(m_Vertices.GetOrAdd<bool>("v:deleted", false));
        m_EDeleted = EdgeProperty<bool>(m_Edges.GetOrAdd<bool>("e:deleted", false));
        m_FDeleted = FaceProperty<bool>(m_Faces.GetOrAdd<bool>("f:deleted", false));
    }

    void Mesh::Clear()
    {
        m_Vertices.Clear();
        m_Halfedges.Clear();
        m_Edges.Clear();
        m_Faces.Clear();

        EnsureProperties();

        m_DeletedVertices = 0;
        m_DeletedEdges = 0;
        m_DeletedFaces = 0;
        m_HasGarbage = false;
    }

    void Mesh::FreeMemory()
    {
        m_Vertices.Shrink_to_fit();
        m_Halfedges.Shrink_to_fit();
        m_Edges.Shrink_to_fit();
        m_Faces.Shrink_to_fit();
    }

    void Mesh::Reserve(std::size_t nVertices, std::size_t nEdges, std::size_t nFaces)
    {
        m_Vertices.Registry().Reserve(nVertices);
        m_Halfedges.Registry().Reserve(2 * nEdges);
        m_Edges.Registry().Reserve(nEdges);
        m_Faces.Registry().Reserve(nFaces);
    }

    VertexHandle Mesh::NewVertex()
    {
        if (VerticesSize() >= kInvalidIndex) return {};
        // PropertySet::PushBack doesn't currently bump registry size; grow explicitly.
        m_Vertices.Resize(VerticesSize() + 1);
        return VertexHandle{static_cast<PropertyIndex>(VerticesSize() - 1)};
    }

    HalfedgeHandle Mesh::NewEdge()
    {
        if (HalfedgesSize() >= kInvalidIndex) return {};

        // One edge => 2 halfedges.
        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        return HalfedgeHandle{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
    }

    HalfedgeHandle Mesh::NewEdge(VertexHandle start, VertexHandle end)
    {
        assert(start != end);
        if (HalfedgesSize() >= kInvalidIndex) return {};

        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        const HalfedgeHandle h0{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
        const HalfedgeHandle h1{static_cast<PropertyIndex>(HalfedgesSize() - 1)};

        SetVertex(h0, end);
        SetVertex(h1, start);

        return h0;
    }

    FaceHandle Mesh::NewFace()
    {
        if (FacesSize() >= kInvalidIndex) return {};
        m_Faces.Resize(FacesSize() + 1);
        return FaceHandle{static_cast<PropertyIndex>(FacesSize() - 1)};
    }

    VertexHandle Mesh::AddVertex()
    {
        return NewVertex();
    }

    VertexHandle Mesh::AddVertex(glm::vec3 position)
    {
        const VertexHandle v = NewVertex();
        if (v.IsValid())
        {
            m_VPoint[v] = position;
        }
        return v;
    }

    bool Mesh::IsBoundary(VertexHandle v) const
    {
        const HalfedgeHandle h = Halfedge(v);
        return !(h.IsValid() && Face(h).IsValid());
    }

    bool Mesh::IsManifold(VertexHandle v) const
    {
        int gaps = 0;
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            std::size_t safety = 0;
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                if (IsBoundary(h)) ++gaps;
                h = CWRotatedHalfedge(h);
                if (++safety > maxIter) break;
            } while (h != start);
        }
        return gaps < 2;
    }

    HalfedgeHandle Mesh::Halfedge(EdgeHandle e, unsigned int i) const
    {
        assert(i <= 1);
        return HalfedgeHandle{static_cast<PropertyIndex>((e.Index << 1u) + i)};
    }

    bool Mesh::IsBoundary(EdgeHandle e) const
    {
        return IsBoundary(Halfedge(e, 0)) || IsBoundary(Halfedge(e, 1));
    }

    bool Mesh::IsBoundary(FaceHandle f) const
    {
        HalfedgeHandle h = Halfedge(f);
        const HalfedgeHandle start = h;
        std::size_t safety = 0;
        const std::size_t maxIter = HalfedgesSize();
        do
        {
            if (IsBoundary(OppositeHalfedge(h))) return true;
            h = NextHalfedge(h);
            if (++safety > maxIter) break;
        } while (h != start);
        return false;
    }

    void Mesh::SetNextHalfedge(HalfedgeHandle h, HalfedgeHandle next)
    {
        m_HConn[h].Next = next;
        m_HConn[next].Prev = h;
    }

    void Mesh::SetPrevHalfedge(HalfedgeHandle h, HalfedgeHandle prev)
    {
        m_HConn[h].Prev = prev;
        m_HConn[prev].Next = h;
    }

    void Mesh::AdjustOutgoingHalfedge(VertexHandle v)
    {
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                if (IsBoundary(h))
                {
                    SetHalfedge(v, h);
                    return;
                }
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) return; // safety: broken connectivity
            } while (h != start);
        }
    }

    std::optional<HalfedgeHandle> Mesh::FindHalfedge(VertexHandle start, VertexHandle end) const
    {
        assert(IsValid(start) && IsValid(end));

        HalfedgeHandle h = Halfedge(start);
        const HalfedgeHandle hh = h;

        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                if (ToVertex(h) == end) return h;
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) break;
            } while (h != hh);
        }

        return std::nullopt;
    }

    std::optional<EdgeHandle> Mesh::FindEdge(VertexHandle a, VertexHandle b) const
    {
        if (auto h = FindHalfedge(a, b)) return Edge(*h);
        return std::nullopt;
    }

    std::optional<FaceHandle> Mesh::AddTriangle(VertexHandle v0, VertexHandle v1, VertexHandle v2)
    {
        m_AddFaceVertices.assign({v0, v1, v2});
        return AddFace(m_AddFaceVertices);
    }

    std::optional<FaceHandle> Mesh::AddQuad(VertexHandle v0, VertexHandle v1, VertexHandle v2, VertexHandle v3)
    {
        m_AddFaceVertices.assign({v0, v1, v2, v3});
        return AddFace(m_AddFaceVertices);
    }

    std::optional<FaceHandle> Mesh::AddFace(std::span<const VertexHandle> vertices)
    {
        const std::size_t n = vertices.size();
        assert(n > 2);

        VertexHandle v;
        std::size_t i, ii, id;
        HalfedgeHandle inner_next, inner_prev, outer_next, outer_prev, boundary_next, boundary_prev, patch_start,
            patch_end;

        auto& halfedges = m_AddFaceHalfedges;
        auto& is_new = m_AddFaceIsNew;
        auto& needs_adjust = m_AddFaceNeedsAdjust;
        auto& next_cache = m_AddFaceNextCache;

        halfedges.clear();
        halfedges.resize(n, HalfedgeHandle{});
        is_new.clear();
        is_new.resize(n, false);
        needs_adjust.clear();
        needs_adjust.resize(n, false);
        next_cache.clear();
        next_cache.reserve(3 * n);

        for (i = 0, ii = 1; i < n; ++i, ++ii, ii %= n)
        {
            if (!IsBoundary(vertices[i]))
            {
                return std::nullopt;
            }

            if (auto h = FindHalfedge(vertices[i], vertices[ii]))
            {
                halfedges[i] = *h;
            }
            else
            {
                is_new[i] = true;
            }

            if (!is_new[i] && !IsBoundary(halfedges[i]))
            {
                return std::nullopt;
            }
        }

        for (i = 0, ii = 1; i < n; ++i, ++ii, ii %= n)
        {
            if (!is_new[i] && !is_new[ii])
            {
                inner_prev = halfedges[i];
                inner_next = halfedges[ii];

                if (NextHalfedge(inner_prev) != inner_next)
                {
                    outer_prev = OppositeHalfedge(inner_next);
                    outer_next = OppositeHalfedge(inner_prev);
                    boundary_prev = outer_prev;
                    do
                    {
                        boundary_prev = OppositeHalfedge(NextHalfedge(boundary_prev));
                    } while (!IsBoundary(boundary_prev) || boundary_prev == inner_prev);

                    boundary_next = NextHalfedge(boundary_prev);
                    assert(IsBoundary(boundary_prev));
                    assert(IsBoundary(boundary_next));

                    if (boundary_next == inner_next)
                    {
                        return std::nullopt;
                    }

                    patch_start = NextHalfedge(inner_prev);
                    patch_end = PrevHalfedge(inner_next);

                    next_cache.emplace_back(boundary_prev, patch_start);
                    next_cache.emplace_back(patch_end, boundary_next);
                    next_cache.emplace_back(inner_prev, inner_next);
                }
            }
        }

        for (i = 0, ii = 1; i < n; ++i, ++ii, ii %= n)
        {
            if (is_new[i])
            {
                halfedges[i] = NewEdge(vertices[i], vertices[ii]);
            }
        }

        const FaceHandle f = NewFace();
        SetHalfedge(f, halfedges[n - 1]);

        for (i = 0, ii = 1; i < n; ++i, ++ii, ii %= n)
        {
            v = vertices[ii];
            inner_prev = halfedges[i];
            inner_next = halfedges[ii];

            id = 0;
            if (is_new[i]) id |= 1;
            if (is_new[ii]) id |= 2;

            if (id)
            {
                outer_prev = OppositeHalfedge(inner_next);
                outer_next = OppositeHalfedge(inner_prev);

                switch (id)
                {
                case 1:
                    boundary_prev = PrevHalfedge(inner_next);
                    next_cache.emplace_back(boundary_prev, outer_next);
                    SetHalfedge(v, outer_next);
                    break;
                case 2:
                    boundary_next = NextHalfedge(inner_prev);
                    next_cache.emplace_back(outer_prev, boundary_next);
                    SetHalfedge(v, boundary_next);
                    break;
                case 3:
                    if (!Halfedge(v).IsValid())
                    {
                        SetHalfedge(v, outer_next);
                        next_cache.emplace_back(outer_prev, outer_next);
                    }
                    else
                    {
                        boundary_next = Halfedge(v);
                        boundary_prev = PrevHalfedge(boundary_next);
                        next_cache.emplace_back(boundary_prev, outer_next);
                        next_cache.emplace_back(outer_prev, boundary_next);
                    }
                    break;
                }

                next_cache.emplace_back(inner_prev, inner_next);
            }
            else
            {
                needs_adjust[ii] = (Halfedge(v) == inner_next);
            }

            SetFace(halfedges[i], f);
        }

        for (const auto& [first, second] : next_cache)
        {
            SetNextHalfedge(first, second);
        }

        for (i = 0; i < n; ++i)
        {
            if (needs_adjust[i])
            {
                AdjustOutgoingHalfedge(vertices[i]);
            }
        }

        return f;
    }

    std::size_t Mesh::Valence(VertexHandle v) const
    {
        std::size_t count = 0;
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                ++count;
                h = CWRotatedHalfedge(h);
                if (count > maxIter) return count; // safety: broken connectivity
            } while (h != start);
        }
        return count;
    }

    std::size_t Mesh::Valence(FaceHandle f) const
    {
        std::size_t count = 0;
        HalfedgeHandle h = Halfedge(f);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                ++count;
                h = NextHalfedge(h);
                if (count > maxIter) break;
            } while (h != start);
        }
        return count;
    }

    void Mesh::DeleteVertex(VertexHandle v)
    {
        if (IsDeleted(v)) return;

        // Collect incident faces first (since DeleteFace mutates connectivity)
        std::vector<FaceHandle> incident;
        incident.reserve(6);

        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            std::size_t safety = 0;
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                const FaceHandle f = Face(h);
                if (f.IsValid()) incident.push_back(f);
                h = CWRotatedHalfedge(h);
                if (++safety > maxIter) break;
            } while (h != start);
        }

        for (auto f : incident) DeleteFace(f);

        if (!m_VDeleted[v])
        {
            m_VDeleted[v] = true;
            ++m_DeletedVertices;
            m_HasGarbage = true;
        }
    }

    void Mesh::DeleteEdge(EdgeHandle e)
    {
        if (IsDeleted(e)) return;

        const FaceHandle f0 = Face(Halfedge(e, 0));
        const FaceHandle f1 = Face(Halfedge(e, 1));

        if (f0.IsValid()) DeleteFace(f0);
        if (f1.IsValid()) DeleteFace(f1);
    }

    void Mesh::DeleteFace(FaceHandle f)
    {
        if (m_FDeleted[f]) return;

        m_FDeleted[f] = true;
        ++m_DeletedFaces;

        std::vector<EdgeHandle> deleted_edges;
        deleted_edges.reserve(3);

        std::vector<VertexHandle> verts;
        verts.reserve(3);

        HalfedgeHandle h = Halfedge(f);
        const HalfedgeHandle start = h;
        std::size_t safety = 0;
        const std::size_t maxIter = HalfedgesSize();
        do
        {
            SetFace(h, {});

            if (IsBoundary(OppositeHalfedge(h)))
            {
                deleted_edges.push_back(Edge(h));
            }

            verts.push_back(ToVertex(h));
            h = NextHalfedge(h);
            if (++safety > maxIter) break;
        } while (h != start);

        if (!deleted_edges.empty())
        {
            for (const auto& edge_handle : deleted_edges)
            {
                const auto h0 = Halfedge(edge_handle, 0);
                const auto v0 = ToVertex(h0);
                const auto next0 = NextHalfedge(h0);
                const auto prev0 = PrevHalfedge(h0);

                const auto h1 = Halfedge(edge_handle, 1);
                const auto v1 = ToVertex(h1);
                const auto next1 = NextHalfedge(h1);
                const auto prev1 = PrevHalfedge(h1);

                SetNextHalfedge(prev0, next1);
                SetNextHalfedge(prev1, next0);

                if (!m_EDeleted[edge_handle])
                {
                    m_EDeleted[edge_handle] = true;
                    ++m_DeletedEdges;
                }

                if (Halfedge(v0) == h1)
                {
                    if (next0 == h1)
                    {
                        if (!m_VDeleted[v0])
                        {
                            m_VDeleted[v0] = true;
                            ++m_DeletedVertices;
                        }
                    }
                    else
                    {
                        SetHalfedge(v0, next0);
                    }
                }

                if (Halfedge(v1) == h0)
                {
                    if (next1 == h0)
                    {
                        if (!m_VDeleted[v1])
                        {
                            m_VDeleted[v1] = true;
                            ++m_DeletedVertices;
                        }
                    }
                    else
                    {
                        SetHalfedge(v1, next1);
                    }
                }
            }
        }

        for (auto vtx : verts) AdjustOutgoingHalfedge(vtx);

        m_HasGarbage = true;
    }

    void Mesh::GarbageCollection()
    {
        if (!m_HasGarbage) return;

        auto nv = VerticesSize();
        auto ne = EdgesSize();
        auto nh = HalfedgesSize();
        auto nf = FacesSize();

        // Defensive: PropertyIndex is the handle index type; garbage collection relies on safe casts.
        // If you ever support > 2^32 elements, replace the handle type or do chunked compaction.
        assert(nv <= std::numeric_limits<PropertyIndex>::max());
        assert(ne <= std::numeric_limits<PropertyIndex>::max());
        assert(nh <= std::numeric_limits<PropertyIndex>::max());
        assert(nf <= std::numeric_limits<PropertyIndex>::max());

        auto vmap = VertexProperty<VertexHandle>(m_Vertices.Add<VertexHandle>("v:garbage-collection", {}));
        auto hmap = HalfedgeProperty<HalfedgeHandle>(m_Halfedges.Add<HalfedgeHandle>("h:garbage-collection", {}));
        auto fmap = FaceProperty<FaceHandle>(m_Faces.Add<FaceHandle>("f:garbage-collection", {}));

        for (std::size_t i = 0; i < nv; ++i) vmap[VertexHandle{static_cast<PropertyIndex>(i)}] = VertexHandle{static_cast<PropertyIndex>(i)};
        for (std::size_t i = 0; i < nh; ++i) hmap[HalfedgeHandle{static_cast<PropertyIndex>(i)}] = HalfedgeHandle{static_cast<PropertyIndex>(i)};
        for (std::size_t i = 0; i < nf; ++i) fmap[FaceHandle{static_cast<PropertyIndex>(i)}] = FaceHandle{static_cast<PropertyIndex>(i)};

        // During compaction the transient map properties are swapped together with
        // the topology payload. Starting from identity maps, that permutation is
        // exactly the old-handle -> new-handle remap we need, so do not swap the
        // map entries a second time here.
        auto swap_vertex_slots = [&](std::size_t a, std::size_t b)
        {
            m_Vertices.Swap(a, b);
        };
        auto swap_edge_slots = [&](std::size_t a, std::size_t b)
        {
            m_Edges.Swap(a, b);

            const std::size_t ha0 = 2 * a;
            const std::size_t ha1 = 2 * a + 1;
            const std::size_t hb0 = 2 * b;
            const std::size_t hb1 = 2 * b + 1;

            m_Halfedges.Swap(ha0, hb0);
            m_Halfedges.Swap(ha1, hb1);
        };
        auto swap_face_slots = [&](std::size_t a, std::size_t b)
        {
            m_Faces.Swap(a, b);
        };

        if (nv > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = nv - 1;
            while (true)
            {
                while (!m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;
                swap_vertex_slots(i0, i1);
            }
            nv = m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
        }

        if (ne > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = ne - 1;
            while (true)
            {
                while (!m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;

                swap_edge_slots(i0, i1);
            }
            ne = m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
            nh = 2 * ne;
        }

        if (nf > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = nf - 1;
            while (true)
            {
                while (!m_FDeleted[FaceHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_FDeleted[FaceHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;
                swap_face_slots(i0, i1);
            }
            nf = m_FDeleted[FaceHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
        }

        // Remap Vertex, Next, and Face through the compaction maps.
        // Prev is rebuilt from Next afterward because Collapse may leave
        // stale Prev pointers (the BCG reference implementation avoids this
        // by not storing Prev at all).
        for (std::size_t i = 0; i < nh; ++i)
        {
            auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            m_HConn[h].Vertex = vmap[VertexHandle{m_HConn[h].Vertex}];
            m_HConn[h].Next = hmap[HalfedgeHandle{m_HConn[h].Next}];
            if (m_HConn[h].Face.IsValid())
            {
                m_HConn[h].Face = fmap[FaceHandle{m_HConn[h].Face}];
            }
        }

        // Rebuild all Prev pointers from the (now-remapped) Next chain.
        for (std::size_t i = 0; i < nh; ++i)
        {
            auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            auto n = m_HConn[h].Next;
            m_HConn[n].Prev = h;
        }

        // Face representatives can go stale the same way vertex representatives do
        // when topology edits delete the particular halfedge a face used as its
        // handle. Rebuild them from the surviving halfedge set instead of blindly
        // remapping a possibly deleted representative.
        for (std::size_t i = 0; i < nf; ++i)
        {
            SetHalfedge(FaceHandle{static_cast<PropertyIndex>(i)}, HalfedgeHandle{});
        }

        for (std::size_t i = 0; i < nh; ++i)
        {
            auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            if (!IsBoundary(h))
            {
                auto f = Face(h);
                if (!Halfedge(f).IsValid())
                {
                    SetHalfedge(f, h);
                }
            }
        }

        // Vertex representatives are less trustworthy than face cycles after
        // topology edits: a live vertex may still point at a deleted-edge
        // halfedge before compaction. Rebuild outgoing halfedges from the
        // surviving halfedge set instead of blindly remapping stale handles.
        for (std::size_t i = 0; i < nv; ++i)
        {
            SetHalfedge(VertexHandle{static_cast<PropertyIndex>(i)}, HalfedgeHandle{});
        }

        for (std::size_t i = 0; i < nh; ++i)
        {
            auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            auto from = FromVertex(h);
            if (!Halfedge(from).IsValid())
            {
                SetHalfedge(from, h);
            }
        }

        for (std::size_t i = 0; i < nv; ++i)
        {
            auto v = VertexHandle{static_cast<PropertyIndex>(i)};
            if (Halfedge(v).IsValid())
            {
                AdjustOutgoingHalfedge(v);
            }
        }

        m_Vertices.Remove(vmap);
        m_Halfedges.Remove(hmap);
        m_Faces.Remove(fmap);

        m_Vertices.Resize(nv);
        m_Vertices.Shrink_to_fit();
        m_Halfedges.Resize(nh);
        m_Halfedges.Shrink_to_fit();
        m_Edges.Resize(ne);
        m_Edges.Shrink_to_fit();
        m_Faces.Resize(nf);
        m_Faces.Shrink_to_fit();

        m_DeletedVertices = m_DeletedEdges = m_DeletedFaces = 0;
        m_HasGarbage = false;
    }

    // =========================================================================
    // IsCollapseOk — Link condition check
    // =========================================================================
    //
    // The link condition (Dey & Edelsbrunner) ensures that edge collapse
    // preserves the topological type of the mesh. For interior edge (v0, v1):
    //   |link(v0) ∩ link(v1)| must equal exactly 2 (the two opposite vertices).
    // For boundary edge: intersection must equal 1.

    bool Mesh::IsCollapseOk(HalfedgeHandle h) const
    {
        return h.IsValid() && !IsDeleted(h) && IsCollapseOk(Edge(h));
    }

    bool Mesh::IsCollapseOk(EdgeHandle e) const
    {
        if (IsDeleted(e)) return false;

        HalfedgeHandle h0 = Halfedge(e, 0);
        HalfedgeHandle h1 = Halfedge(e, 1);

        VertexHandle v0 = ToVertex(h1); // FromVertex of h0
        VertexHandle v1 = ToVertex(h0);

        if (IsDeleted(v0) || IsDeleted(v1)) return false;
        if (IsIsolated(v0) || IsIsolated(v1)) return false;

        // Collect 1-ring neighbors of v0 (with safety limit)
        const std::size_t maxIter = HalfedgesSize();
        std::vector<VertexHandle> link0;
        link0.reserve(8);
        {
            HalfedgeHandle h = Halfedge(v0);
            HalfedgeHandle start = h;
            std::size_t iter = 0;
            do
            {
                VertexHandle vn = ToVertex(h);
                if (vn != v1) link0.push_back(vn);
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) return false;  // broken connectivity
            } while (h != start);
        }

        // Collect 1-ring neighbors of v1 (with safety limit)
        std::vector<VertexHandle> link1;
        link1.reserve(8);
        {
            HalfedgeHandle h = Halfedge(v1);
            HalfedgeHandle start = h;
            std::size_t iter = 0;
            do
            {
                VertexHandle vn = ToVertex(h);
                if (vn != v0) link1.push_back(vn);
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) return false;  // broken connectivity
            } while (h != start);
        }

        // Sort for intersection
        std::sort(link0.begin(), link0.end(), [](VertexHandle a, VertexHandle b) { return a.Index < b.Index; });
        std::sort(link1.begin(), link1.end(), [](VertexHandle a, VertexHandle b) { return a.Index < b.Index; });

        // Count intersection
        std::size_t commonCount = 0;
        auto it0 = link0.begin();
        auto it1 = link1.begin();
        while (it0 != link0.end() && it1 != link1.end())
        {
            if (it0->Index < it1->Index) ++it0;
            else if (it0->Index > it1->Index) ++it1;
            else { ++commonCount; ++it0; ++it1; }
        }

        // For interior edge: exactly 2 common neighbors (the two opposite vertices)
        // For boundary edge: exactly 1
        bool isBoundaryEdge = IsBoundary(e);
        std::size_t expected = isBoundaryEdge ? 1u : 2u;

        if (commonCount != expected) return false;

        return true;
    }

    // =========================================================================
    // IsFlipOk — Edge flip validity check
    // =========================================================================

    bool Mesh::IsFlipOk(EdgeHandle e) const
    {
        if (IsDeleted(e)) return false;

        HalfedgeHandle h0 = Halfedge(e, 0);
        HalfedgeHandle h1 = Halfedge(e, 1);

        // Must be interior edge
        if (IsBoundary(h0) || IsBoundary(h1)) return false;

        // Both faces must be triangles
        FaceHandle f0 = Face(h0);
        FaceHandle f1 = Face(h1);
        if (Valence(f0) != 3 || Valence(f1) != 3) return false;

        // The two opposite vertices
        VertexHandle vc = ToVertex(NextHalfedge(h0));
        VertexHandle vd = ToVertex(NextHalfedge(h1));

        // Check that the new edge doesn't already exist
        if (FindEdge(vc, vd).has_value()) return false;

        // Don't flip if it would create a valence-2 vertex
        VertexHandle va = ToVertex(h1); // FromVertex(h0)
        VertexHandle vb = ToVertex(h0);
        if (Valence(va) <= 3 || Valence(vb) <= 3) return false;

        return true;
    }

    // =========================================================================
    // Collapse — Edge collapse (PMP/BCG pattern)
    // =========================================================================
    //
    // The directed overload collapses halfedge h0 by removing FromVertex(h0)
    // and keeping ToVertex(h0) at newPosition. Uses the standard
    // remove_edge_helper + remove_loop_helper decomposition (Botsch, Kobbelt,
    // Pauly, Alliez — "Polygon Mesh Processing") which correctly splices
    // deleted halfedges out of all Next/Prev chains.

    std::optional<VertexHandle> Mesh::Collapse(EdgeHandle e, glm::vec3 newPosition)
    {
        return Collapse(Halfedge(e, 1), newPosition);
    }

    std::optional<VertexHandle> Mesh::Collapse(HalfedgeHandle h0, glm::vec3 newPosition)
    {
        if (!IsCollapseOk(h0)) return std::nullopt;

        // h0 goes from vRemove to vSurvive.
        VertexHandle vSurvive = ToVertex(h0);
        VertexHandle vRemove  = FromVertex(h0);

        // Transfer vertex attributes BEFORE deletion.
        TransferVertexAttributes_OnCollapse(vSurvive, vRemove, vSurvive, newPosition);

        // Save these BEFORE remove_edge_helper modifies the mesh.
        HalfedgeHandle h1 = PrevHalfedge(h0);
        HalfedgeHandle o0 = OppositeHalfedge(h0);
        HalfedgeHandle o1 = NextHalfedge(o0);

        // ---- remove_edge_helper(h0) ----
        {
            HalfedgeHandle hn = NextHalfedge(h0);
            HalfedgeHandle hp = PrevHalfedge(h0); // == h1
            HalfedgeHandle on = NextHalfedge(o0);  // == o1
            HalfedgeHandle op = PrevHalfedge(o0);

            FaceHandle fh = Face(h0);
            FaceHandle fo = Face(o0);

            // Redirect all halfedges whose to-vertex is vRemove → vSurvive.
            // Walk outgoing halfedges of vRemove; for each, change the
            // to-vertex of its opposite from vRemove to vSurvive.
            {
                HalfedgeHandle cur = Halfedge(vRemove);
                if (cur.IsValid())
                {
                    HalfedgeHandle start = cur;
                    std::size_t safety = 0;
                    const std::size_t maxIter = HalfedgesSize();
                    do
                    {
                        SetVertex(OppositeHalfedge(cur), vSurvive);
                        cur = CWRotatedHalfedge(cur);
                        if (++safety > maxIter) break;
                    } while (cur != start);
                }
            }

            // Splice h0 and o0 out of their Next chains.
            SetNextHalfedge(hp, hn);
            SetNextHalfedge(op, on);

            // Update face halfedge representatives.
            if (fh.IsValid()) SetHalfedge(fh, hn);
            if (fo.IsValid()) SetHalfedge(fo, on);

            // Update vertex halfedge representative for the survivor.
            if (Halfedge(vSurvive) == o0)
                SetHalfedge(vSurvive, hn);
            AdjustOutgoingHalfedge(vSurvive);

            // Delete the collapsed edge and the removed vertex.
            m_EDeleted[Edge(h0)] = true;
            ++m_DeletedEdges;
            m_VDeleted[vRemove] = true;
            ++m_DeletedVertices;
        }

        // ---- Check for degenerate 2-gon loops ----
        // If an adjacent face was a triangle, removing h0/o0 leaves a 2-gon.
        if (NextHalfedge(NextHalfedge(h1)) == h1)
            RemoveLoopHelper(h1);

        if (NextHalfedge(NextHalfedge(o1)) == o1)
            RemoveLoopHelper(o1);

        // Set survivor position.
        Position(vSurvive) = newPosition;

        m_HasGarbage = true;
        return vSurvive;
    }

    // =========================================================================
    // RemoveLoopHelper — remove a degenerate 2-gon face
    // =========================================================================
    //
    // Called when a face has degenerated into a 2-gon (two halfedges forming a
    // loop: next(next(h)) == h). Merges the duplicate edges and deletes the
    // degenerate face. Follows the PMP/BCG reference implementation.

    void Mesh::RemoveLoopHelper(HalfedgeHandle h)
    {
        HalfedgeHandle h0 = h;
        HalfedgeHandle h1 = NextHalfedge(h0);

        HalfedgeHandle o0 = OppositeHalfedge(h0);
        HalfedgeHandle o1 = OppositeHalfedge(h1);

        VertexHandle v0 = ToVertex(h0);
        VertexHandle v1 = ToVertex(h1);

        FaceHandle fh = Face(h0);
        FaceHandle fo = Face(o0);

        // h1 replaces o0 in o0's face chain.
        // h1 goes in the SAME direction as o0 (both connect the same
        // pair of vertices in the same orientation), so the chain stays
        // consistent. Edge(h0) is deleted; edge(h1) survives with h1
        // in the external face (fo) and o1 in its own face.
        SetNextHalfedge(PrevHalfedge(o0), h1);
        SetNextHalfedge(h1, NextHalfedge(o0));
        SetFace(h1, fo);

        // Fix vertex representatives.
        SetHalfedge(v0, h1);
        AdjustOutgoingHalfedge(v0);
        SetHalfedge(v1, o1);
        AdjustOutgoingHalfedge(v1);

        // Fix face representative.
        if (fo.IsValid() && Halfedge(fo) == o0)
            SetHalfedge(fo, h1);

        // Delete the degenerate face and the duplicate edge.
        if (fh.IsValid())
        {
            m_FDeleted[fh] = true;
            ++m_DeletedFaces;
        }

        m_EDeleted[Edge(h0)] = true;
        ++m_DeletedEdges;
    }

    // =========================================================================
    // Flip — Edge flip
    // =========================================================================
    //
    //  Before:           After:
    //     c                 c
    //    / \              / | \
    //   / f0\            /  |  \
    //  a-----b          a  f0'  b
    //   \ f1/            \  |  /
    //    \ /              \ | /
    //     d                 d
    //
    //  Edge (a,b) becomes edge (c,d).

    bool Mesh::Flip(EdgeHandle e)
    {
        if (!IsFlipOk(e)) return false;

        HalfedgeHandle h0 = Halfedge(e, 0);  // a → b
        HalfedgeHandle h1 = Halfedge(e, 1);  // b → a

        // Halfedges in face f0 (a → b → c → a)
        HalfedgeHandle h0n = NextHalfedge(h0);  // b → c
        HalfedgeHandle h0p = PrevHalfedge(h0);  // c → a

        // Halfedges in face f1 (b → a → d → b)
        HalfedgeHandle h1n = NextHalfedge(h1);  // a → d
        HalfedgeHandle h1p = PrevHalfedge(h1);  // d → b

        FaceHandle f0 = Face(h0);
        FaceHandle f1 = Face(h1);

        VertexHandle va = FromVertex(h0);      // = ToVertex(h1)
        VertexHandle vb = ToVertex(h0);
        VertexHandle vc = ToVertex(h0n);       // opposite vertex in f0
        VertexHandle vd = ToVertex(h1n);       // opposite vertex in f1

        // Update the flipped edge endpoints: h0 becomes c→d, h1 becomes d→c
        SetVertex(h0, vd);   // h0 now points to d
        SetVertex(h1, vc);   // h1 now points to c

        // Rewire face f0: c → d → a → c  (h0, h1n, h0p)
        SetNextHalfedge(h0, h1n);   // c→d is followed by a→(old d→b becomes)... wait
        // Let me be more precise:
        // After flip: h0 = c → d,  we want f0 = (c,d,a)
        //   h0 (c→d), then h1n (a→d, but we need d→a... no)
        //
        // Actually let me reconsider the rewiring carefully.
        // h0 goes from c to d (was a to b)
        // h1 goes from d to c (was b to a)
        //
        // Face f0 should be (c, d, a): h0 (c→d), then h1p (d→b → but b is wrong...)
        //
        // Let me think again. After the flip:
        // h0: c → d (face f0)
        // h1: d → c (face f1)
        //
        // Face f0 = triangle (c, d, a):
        //   h0 (c → d) → h1n (a → d? no...)
        //
        // I need to be more careful. Let me re-derive.
        //
        // Before flip:
        //   f0: h0 (a→b), h0n (b→c), h0p (c→a)
        //   f1: h1 (b→a), h1n (a→d), h1p (d→b)
        // After flip, edge becomes (c,d):
        //   h0: c → d
        //   h1: d → c
        //
        //   f0 should be triangle (c, d, a):
        //     h0 (c→d) → h1n(a→d)→h0(... wait, h0 is c→d, not d→c)
        //     No. f0 = (c, d, a): h0(c→d)→?(d→a)→h0p(... h0p was c→a, but we need a→c)
        //
        // I think the cleaner approach is:
        //   f0 = {h0, h1p, h0n} with next chain: h0→h1p→h0n→h0
        //   f1 = {h1, h0p, h1n} with next chain: h1→h0n→h1n→h1
        //   This gives:
        //     f0: c→d (h0), d→(h1p.to)=b → wrong, we want d→a
        //
        //   Face 0: triangle (c, d, a):
        //     Edge c→d: h0
        //     Edge d→a: I need a halfedge from d to a. That's not any of my 6.
        //     Edge a→c: I need a halfedge from a to c. h0n goes b→c, not a→c.
        //
        // This means I need to reassign h1p and h0p:
        //   Face 0 = (vc, vd, va): h0 (vc→vd), then I reuse h1p but change its TO?
        //   No, that would change the edge endpoint.
        //
        // I think the standard flip recipe reassigns the next/prev pointers of the
        // 6 halfedges among the 2 faces differently:
        //
        //   New Face 0: h0, h1p, h0n   (Wait, is this right?)
        //     h0: vc→vd, h1p: vd→vb, h0n: vb→vc → Triangle (vc, vd, vb)
        //   New Face 1: h1, h0p, h1n   (No...)
        //     h1: vd→vc, h0p: vc→va, h1n: va→vd → Triangle (vd, vc, va)
        //
        //   This gives triangles (vc, vd, vb) and (vd, vc, va) = (va, vd, vc).
        //   The edge is (vc, vd) = (c, d). One triangle has vertices {a, c, d},
        //   the other has {b, c, d}. That's correct!
        //
        //   Face 0: h0→h1p→h0n→h0  → (vc→vd→vb→vc)
        //   Face 1: h1→h0p→h1n→h1  → (vd→vc→va→vd)

        // Set next pointers for face f0: h0 → h1p → h0n → h0
        SetNextHalfedge(h0, h1p);
        SetNextHalfedge(h1p, h0n);
        SetNextHalfedge(h0n, h0);

        // Set next pointers for face f1: h1 → h0p → h1n → h1
        SetNextHalfedge(h1, h0p);
        SetNextHalfedge(h0p, h1n);
        SetNextHalfedge(h1n, h1);

        // Set face pointers
        SetFace(h0, f0);
        SetFace(h1p, f0);
        SetFace(h0n, f0);

        SetFace(h1, f1);
        SetFace(h0p, f1);
        SetFace(h1n, f1);

        // Update face halfedge references
        SetHalfedge(f0, h0);
        SetHalfedge(f1, h1);

        // Update vertex outgoing halfedges (va and vb lost an adjacent face each)
        if (Halfedge(va) == h0) SetHalfedge(va, h1n);
        if (Halfedge(vb) == h1) SetHalfedge(vb, h0n);

        AdjustOutgoingHalfedge(va);
        AdjustOutgoingHalfedge(vb);
        AdjustOutgoingHalfedge(vc);
        AdjustOutgoingHalfedge(vd);

        return true;
    }

    // =========================================================================
    // Split — Edge split
    // =========================================================================
    //
    // Splits edge e = (va, vb) by inserting a new vertex vm at `position`.
    //
    //  Before (interior):       After:
    //       c                      c
    //      / \                   / | \
    //     / f0\                /  f0 f2
    //    a-----b              a---vm---b
    //     \ f1/                \  f1 f3
    //      \ /                   \ | /
    //       d                      d
    //
    // Creates 1 vertex, 3 edges, 2 faces (interior) or 1 face (boundary).

    VertexHandle Mesh::Split(EdgeHandle e, glm::vec3 position)
    {
        if (IsDeleted(e)) return {};

        HalfedgeHandle h0 = Halfedge(e, 0);  // va → vb
        HalfedgeHandle h1 = Halfedge(e, 1);  // vb → va

        VertexHandle va = FromVertex(h0);
        VertexHandle vb = ToVertex(h0);

        bool hasFace0 = !IsBoundary(h0);
        bool hasFace1 = !IsBoundary(h1);

        // Gather adjacent topology before modification
        HalfedgeHandle h0n{}, h0p{}, h1n{}, h1p{};
        VertexHandle vc{}, vd{};
        FaceHandle f0{}, f1{};

        if (hasFace0)
        {
            h0n = NextHalfedge(h0);
            h0p = PrevHalfedge(h0);
            vc = ToVertex(h0n);
            f0 = Face(h0);
        }

        if (hasFace1)
        {
            h1n = NextHalfedge(h1);
            h1p = PrevHalfedge(h1);
            vd = ToVertex(h1n);
            f1 = Face(h1);
        }

        // Create new vertex
        VertexHandle vm = AddVertex(position);

        // Transfer/interpolate any configured vertex attributes.
        // (PMP convention: the inserted vertex receives attributes based on the edge endpoints.)
        TransferVertexAttributes_OnSplit(va, vb, vm, position);

        // Modify existing edge e: now goes va → vm (reuse h0/h1)
        SetVertex(h0, vm);  // h0 now: va → vm
        // h1 already: vb → va, but we need vm → va.
        // Actually we need: e = (va, vm) with h0: va→vm, h1: vm→va
        // But h1.to was va, and h1.from was vb. We need h1.from = vm.
        // h1.from = ToVertex(OppositeHalfedge(h1)) = ToVertex(h0) = vm. Good!
        // Wait, we just set h0.to = vm, so h1.from = vm. Correct.

        // Create edge (vm, vb)
        HalfedgeHandle hNewEdge = NewEdge(vm, vb);
        HalfedgeHandle hNewEdgeOpp = OppositeHalfedge(hNewEdge);
        // hNewEdge: vm → vb
        // hNewEdgeOpp: vb → vm

        // Update vb's outgoing halfedge if needed
        if (Halfedge(vb) == h1)
            SetHalfedge(vb, hNewEdgeOpp);

        // Set vm's outgoing halfedge
        SetHalfedge(vm, h0);

        if (hasFace0)
        {
            // Create edge (vm, vc)
            HalfedgeHandle hSplit0 = NewEdge(vm, vc);
            HalfedgeHandle hSplit0Opp = OppositeHalfedge(hSplit0);
            // hSplit0: vm → vc
            // hSplit0Opp: vc → vm

            // Create new face f2 = (vm, vb, vc)
            FaceHandle f2 = NewFace();

            // Existing face f0 becomes: (va, vm, vc)
            //   h0 (va→vm), hSplit0 (vm→vc), h0p (vc→va)
            SetNextHalfedge(h0, hSplit0);
            SetNextHalfedge(hSplit0, h0p);
            SetNextHalfedge(h0p, h0);
            SetFace(h0, f0);
            SetFace(hSplit0, f0);
            SetFace(h0p, f0);
            SetHalfedge(f0, h0);

            // New face f2: (vm, vb, vc)
            //   hNewEdge (vm→vb), h0n (vb→vc), hSplit0Opp (vc→vm)
            SetNextHalfedge(hNewEdge, h0n);
            SetNextHalfedge(h0n, hSplit0Opp);
            SetNextHalfedge(hSplit0Opp, hNewEdge);
            SetFace(hNewEdge, f2);
            SetFace(h0n, f2);
            SetFace(hSplit0Opp, f2);
            SetHalfedge(f2, hNewEdge);
        }
        else
        {
            // Boundary on h0 side: just link h0 → hNewEdge in the boundary
            HalfedgeHandle hBoundaryNext = NextHalfedge(h0);
            SetNextHalfedge(h0, hNewEdge);
            SetNextHalfedge(hNewEdge, hBoundaryNext);
        }

        if (hasFace1)
        {
            // Create edge (vm, vd)
            HalfedgeHandle hSplit1 = NewEdge(vm, vd);
            HalfedgeHandle hSplit1Opp = OppositeHalfedge(hSplit1);
            // hSplit1: vm → vd
            // hSplit1Opp: vd → vm

            // Create new face f3 = (vm, va, vd) — wait, careful with winding
            FaceHandle f3 = NewFace();

            // Existing face f1 becomes: (vb, vm, vd)
            //   hNewEdgeOpp (vb→vm), hSplit1 (vm→vd), h1p (vd→vb)
            SetNextHalfedge(hNewEdgeOpp, hSplit1);
            SetNextHalfedge(hSplit1, h1p);
            SetNextHalfedge(h1p, hNewEdgeOpp);
            SetFace(hNewEdgeOpp, f1);
            SetFace(hSplit1, f1);
            SetFace(h1p, f1);
            SetHalfedge(f1, hNewEdgeOpp);

            // New face f3: (vm, va, vd) → (h1 (vm→va), h1n (va→vd), hSplit1Opp (vd→vm))
            SetNextHalfedge(h1, h1n);
            SetNextHalfedge(h1n, hSplit1Opp);
            SetNextHalfedge(hSplit1Opp, h1);
            SetFace(h1, f3);
            SetFace(h1n, f3);
            SetFace(hSplit1Opp, f3);
            SetHalfedge(f3, h1);
        }
        else
        {
            // Boundary on h1 side: link hNewEdgeOpp → h1 in the boundary
            HalfedgeHandle hBoundaryPrev = PrevHalfedge(h1);
            SetNextHalfedge(hBoundaryPrev, hNewEdgeOpp);
            SetNextHalfedge(hNewEdgeOpp, h1);
        }

        AdjustOutgoingHalfedge(va);
        AdjustOutgoingHalfedge(vb);
        AdjustOutgoingHalfedge(vm);
        if (vc.IsValid()) AdjustOutgoingHalfedge(vc);
        if (vd.IsValid()) AdjustOutgoingHalfedge(vd);

        return vm;
    }

    // -------------------------------------------------------------------------
    // Bulk edge extraction for GPU upload
    // -------------------------------------------------------------------------

    std::vector<EdgeVertexPair> Mesh::ExtractEdgeVertexPairs() const
    {
        const std::size_t nEdges = EdgesSize();
        if (nEdges == 0)
            return {};

        // Direct span access to halfedge connectivity — avoids per-edge function calls.
        // m_EDeleted is Property<bool> (vector<bool>), so use operator[] instead of Span().
        const auto hConn = m_HConn.Span();

        std::vector<EdgeVertexPair> result;
        result.reserve(EdgeCount());

        for (std::size_t i = 0; i < nEdges; ++i)
        {
            if (m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i)}])
                continue;

            // Edge i has halfedges at indices 2*i and 2*i+1.
            // ToVertex(halfedge 2*i)   = hConn[2*i].Vertex   (one endpoint)
            // ToVertex(halfedge 2*i+1) = hConn[2*i+1].Vertex (other endpoint)
            const auto v0 = static_cast<uint32_t>(hConn[2 * i + 1].Vertex.Index);
            const auto v1 = static_cast<uint32_t>(hConn[2 * i].Vertex.Index);
            result.push_back({v0, v1});
        }

        return result;
    }

    std::size_t Mesh::ExtractEdgeVertexPairs(std::span<EdgeVertexPair> out) const
    {
        const std::size_t nEdges = EdgesSize();
        if (nEdges == 0 || out.empty())
            return 0;

        const auto hConn = m_HConn.Span();

        std::size_t written = 0;

        for (std::size_t i = 0; i < nEdges && written < out.size(); ++i)
        {
            if (m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i)}])
                continue;

            const auto v0 = static_cast<uint32_t>(hConn[2 * i + 1].Vertex.Index);
            const auto v1 = static_cast<uint32_t>(hConn[2 * i].Vertex.Index);
            out[written++] = {v0, v1};
        }

        return written;
    }
}

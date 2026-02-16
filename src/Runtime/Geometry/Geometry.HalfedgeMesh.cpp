module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>
#include <span>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

module Geometry:HalfedgeMesh.Impl;

import :HalfedgeMesh;
import :Properties;

namespace Geometry::Halfedge
{
    Mesh::Mesh()
    {
        EnsureProperties();
    }

    Mesh::Mesh(const Mesh& rhs) = default;
    Mesh::~Mesh() = default;
    Mesh& Mesh::operator=(const Mesh& rhs) = default;

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
            do
            {
                if (IsBoundary(h)) ++gaps;
                h = CWRotatedHalfedge(h);
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
        do
        {
            if (IsBoundary(OppositeHalfedge(h))) return true;
            h = NextHalfedge(h);
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
            do
            {
                if (ToVertex(h) == end) return h;
                h = CWRotatedHalfedge(h);
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
            do
            {
                ++count;
                h = NextHalfedge(h);
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
            do
            {
                const FaceHandle f = Face(h);
                if (f.IsValid()) incident.push_back(f);
                h = CWRotatedHalfedge(h);
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
        do
        {
            SetFace(h, {});

            if (IsBoundary(OppositeHalfedge(h)))
            {
                deleted_edges.push_back(Edge(h));
            }

            verts.push_back(ToVertex(h));
            h = NextHalfedge(h);
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

        // During compaction we swap entire property sets. This includes our transient map properties.
        // To keep maps meaningful (oldHandle -> newHandle), we must also swap the map entries back.
        auto swap_vertex_slots = [&](std::size_t a, std::size_t b)
        {
            m_Vertices.Swap(a, b);
            using std::swap;
            swap(vmap[VertexHandle{static_cast<PropertyIndex>(a)}], vmap[VertexHandle{static_cast<PropertyIndex>(b)}]);
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

            using std::swap;
            swap(hmap[HalfedgeHandle{static_cast<PropertyIndex>(ha0)}], hmap[HalfedgeHandle{static_cast<PropertyIndex>(hb0)}]);
            swap(hmap[HalfedgeHandle{static_cast<PropertyIndex>(ha1)}], hmap[HalfedgeHandle{static_cast<PropertyIndex>(hb1)}]);
        };
        auto swap_face_slots = [&](std::size_t a, std::size_t b)
        {
            m_Faces.Swap(a, b);
            using std::swap;
            swap(fmap[FaceHandle{static_cast<PropertyIndex>(a)}], fmap[FaceHandle{static_cast<PropertyIndex>(b)}]);
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

        for (std::size_t i = 0; i < nv; ++i)
        {
            auto v = VertexHandle{static_cast<PropertyIndex>(i)};
            if (!IsIsolated(v))
            {
                SetHalfedge(v, hmap[Halfedge(v)]);
            }
        }

        for (std::size_t i = 0; i < nh; ++i)
        {
            auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            SetVertex(h, vmap[ToVertex(h)]);
            SetNextHalfedge(h, hmap[NextHalfedge(h)]);
            if (!IsBoundary(h))
            {
                SetFace(h, fmap[Face(h)]);
            }
        }

        for (std::size_t i = 0; i < nf; ++i)
        {
            auto f = FaceHandle{static_cast<PropertyIndex>(i)};
            SetHalfedge(f, hmap[Halfedge(f)]);
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

        // Additional check: don't collapse if it would make the mesh degenerate
        // (e.g., both endpoints are boundary but the edge is interior)
        if (IsBoundary(v0) && IsBoundary(v1) && !isBoundaryEdge)
            return false;

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
    // Collapse — Edge collapse
    // =========================================================================
    //
    // Collapses edge e by merging v1 into v0. v0 survives at newPosition.
    // All halfedges pointing to v1 are redirected to v0. The edge and its
    // adjacent faces are deleted. v1 is marked deleted.

    std::optional<VertexHandle> Mesh::Collapse(EdgeHandle e, glm::vec3 newPosition)
    {
        if (!IsCollapseOk(e)) return std::nullopt;

        HalfedgeHandle h0 = Halfedge(e, 0);
        HalfedgeHandle h1 = Halfedge(e, 1);

        VertexHandle v0 = FromVertex(h0);  // surviving vertex
        VertexHandle v1 = ToVertex(h0);    // removed vertex

        bool hasF0 = !IsBoundary(h0);
        bool hasF1 = !IsBoundary(h1);

        // Collect topology BEFORE modification
        HalfedgeHandle h0n, h0p, h0n_opp, h0p_opp;
        VertexHandle vc;
        FaceHandle f0;
        if (hasF0)
        {
            h0n = NextHalfedge(h0);
            h0p = PrevHalfedge(h0);
            h0n_opp = OppositeHalfedge(h0n);
            h0p_opp = OppositeHalfedge(h0p);
            vc = ToVertex(h0n);
            f0 = Face(h0);
        }

        HalfedgeHandle h1n, h1p, h1n_opp, h1p_opp;
        VertexHandle vd;
        FaceHandle f1;
        if (hasF1)
        {
            h1n = NextHalfedge(h1);
            h1p = PrevHalfedge(h1);
            h1n_opp = OppositeHalfedge(h1n);
            h1p_opp = OppositeHalfedge(h1p);
            vd = ToVertex(h1n);
            f1 = Face(h1);
        }

        // Collect v1's outgoing halfedges before redirect
        std::vector<HalfedgeHandle> v1out;
        v1out.reserve(8);
        {
            HalfedgeHandle h = Halfedge(v1);
            HalfedgeHandle start = h;
            const std::size_t maxIter = HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                v1out.push_back(h);
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) break; // safety: broken connectivity
            } while (h != start);
        }

        // Phase 1: Redirect all v1 references to v0
        for (auto h : v1out)
        {
            SetVertex(OppositeHalfedge(h), v0);
        }

        // Phase 2: Handle degenerate face on h0 side
        if (hasF0)
        {
            // After redirect, h0n now goes v0→vc (was v1→vc).
            // h0p_opp also goes v0→vc. These are duplicate edges.
            // We keep edge(h0p) and delete edge(h0n).
            // h0n_opp (vc→v0) must be spliced into h0p's chain.

            // Splice h0p into the chain where h0n_opp was
            SetNextHalfedge(PrevHalfedge(h0n_opp), h0p);
            SetNextHalfedge(h0p, NextHalfedge(h0n_opp));
            SetFace(h0p, Face(h0n_opp));

            // Splice h0p_opp into the chain where h0n was
            // (h0n was inside the deleted face, so h0p_opp takes its role
            // in external faces — but actually h0p_opp is already in its own chain)
            // No: h0n is being deleted along with its face.
            // h0p_opp is in the chain of whatever face was using it.
            // We just need to make sure face references are updated.

            if (Face(h0n_opp).IsValid())
                SetHalfedge(Face(h0n_opp), h0p);
            if (Halfedge(vc) == h0n_opp)
                SetHalfedge(vc, h0p);

            m_FDeleted[f0] = true;
            ++m_DeletedFaces;

            EdgeHandle eDup = Edge(h0n);
            if (!m_EDeleted[eDup])
            {
                m_EDeleted[eDup] = true;
                ++m_DeletedEdges;
            }
        }

        // Phase 3: Handle degenerate face on h1 side
        if (hasF1)
        {
            // After redirect, h1p now goes vd→v0 (was vd→v1).
            // h1n_opp also goes vd→v0. Duplicate edges.
            // We keep edge(h1n) and delete edge(h1p).
            // h1p_opp (v0→vd) must be spliced into h1n's chain.

            SetNextHalfedge(PrevHalfedge(h1p_opp), h1n);
            SetNextHalfedge(h1n, NextHalfedge(h1p_opp));
            SetFace(h1n, Face(h1p_opp));

            if (Face(h1p_opp).IsValid())
                SetHalfedge(Face(h1p_opp), h1n);
            if (Halfedge(vd) == h1p_opp)
                SetHalfedge(vd, h1n);

            m_FDeleted[f1] = true;
            ++m_DeletedFaces;

            EdgeHandle eDup = Edge(h1p);
            if (!m_EDeleted[eDup])
            {
                m_EDeleted[eDup] = true;
                ++m_DeletedEdges;
            }
        }

        // Phase 4: Delete collapsed edge and vertex v1
        m_EDeleted[e] = true;
        ++m_DeletedEdges;
        m_VDeleted[v1] = true;
        ++m_DeletedVertices;

        // Phase 5: Set v0's position and fix outgoing halfedge
        Position(v0) = newPosition;

        // Find a valid outgoing halfedge for v0
        HalfedgeHandle validOut;
        for (auto h : v1out)
        {
            EdgeHandle eH = Edge(h);
            if (!m_EDeleted[eH])
            {
                validOut = h;
                break;
            }
        }
        if (validOut.IsValid())
            SetHalfedge(v0, validOut);

        AdjustOutgoingHalfedge(v0);
        if (vc.IsValid() && !m_VDeleted[vc]) AdjustOutgoingHalfedge(vc);
        if (vd.IsValid() && !m_VDeleted[vd]) AdjustOutgoingHalfedge(vd);

        m_HasGarbage = true;
        return v0;
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

        // Update the flipped edge endpoints: h0 becomes c → d, h1 becomes d → c
        SetVertex(h0, vd);   // h0 now points to d
        SetVertex(h1, vc);   // h1 now points to c

        // Rewire face f0: c → d → a → c  (h0, h1n, h0p)
        SetNextHalfedge(h0, h1n);   // c→d is followed by a→(old d→b becomes)... wait
        // Let me be more precise:
        // After flip: h0 = c → d,  we want f0 = (c,d,a)
        //   h0 (c→d), then h1n (was a→d, but we need d→a... no)
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
        //
        // After flip, edge becomes (c,d):
        //   h0: c → d
        //   h1: d → c
        //
        //   f0 should be triangle (c, d, a):
        //     h0 (c→d) → h1n (d... wait, h1n was a→d, not d→something)
        //
        // Hmm. Let me use the standard PMP flip algorithm.
        // The key insight: we reuse the existing 6 halfedges.
        // h0 and h1 get new endpoints. The other 4 halfedges get reassigned to faces.

        // Standard approach: h0 becomes c→d, h1 becomes d→c
        // Face f0: h0p (previously c→a, still c→a... no, h0 now starts from c)
        //
        // Let me follow the standard halfedge flip from PMP:

        // Face f0: (c, d, a) = h1 won't work...
        //
        // OK let me use the well-known recipe:
        // After flip, f0 = {h0, h1p, h0p} and f1 = {h1, h0n, h1n}...
        // Wait, that's the Botsch et al. recipe? Let me just use the known-correct version.

        // The standard halfedge flip from "Polygon Mesh Processing" (Botsch et al.):
        // f0 gets: h0 → h1p → h0p  (c→d, d→b... no that's wrong for triangle (c,d,a))
        //
        // Actually the standard recipe is:
        //   f0: h1p, h0, h0n... no.
        //
        // Let me just go step by step.
        // We have 6 halfedges: h0, h0n, h0p, h1, h1n, h1p
        // Before:
        //   f0: h0→h0n→h0p→h0
        //   f1: h1→h1n→h1p→h1
        // After flip: h0 = c→d, h1 = d→c
        //   f0 = (a, c, d): h0p(c→a)→h1n(a→d)→h0(... wait, h0 is c→d not d→c)
        //   No. f0 = (c, d, a): h0(c→d)→?(d→a)→h0p(... h0p was c→a, but we need a→c)
        //
        // I think the cleaner approach is:
        //   f0 = {h0, h1p, h0p} with next chain: h0→h1p→h0p→h0
        //   f1 = {h1, h0n, h1n} with next chain: h1→h0n→h1n→h1
        //   This gives:
        //     f0: c→d (h0), d→(h1p.to)=b → wrong, we want d→a
        //
        // I'm overcomplicating this. Let me just use the well-known recipe from
        // Surface_mesh/PMP library:

        // After flip:
        //   f0: h0 → h0n → h1n → h0     (Wait, that's 3 edges for a triangle)
        //   f1: h1 → h1n → h0n → h1     (No...)
        //
        // Actually the correct recipe from PMP (Botsch et al.) is:
        //   New face f0 = (h1p, h0, h0n) — wait, let me just look at the topology.
        //
        // h0 is now c → d
        // h0n was b → c and remains b → c (not changed)
        // h0p was c → a and remains c → a
        // h1 is now d → c
        // h1n was a → d and remains a → d
        // h1p was d → b and remains d → b
        //
        // Triangle 1: (a, c, d) using halfedges: h0p (c→a... that's backwards)
        //   Actually h0p goes from c to a. So reading endpoints:
        //   h0p: c→a, h1n: a→d, h0: c→d (but h0 goes c→d, not d→c)
        //   For triangle (a, c, d), we need: a→c, c→d, d→a
        //   a→c = opposite of h0p... no, that's wrong.
        //
        // OK I think the confusion is because h0 and h1 have swapped *which vertex they point to*
        // but the halfedge *pair* still represents the same edge slot. h0 is the even halfedge
        // and h1 is the odd. After SetVertex, h0 now points TO vd and h1 points TO vc.
        // But FromVertex(h0) = ToVertex(h1) = vc. FromVertex(h1) = ToVertex(h0) = vd.
        //
        // So: h0 goes from vc to vd, h1 goes from vd to vc. Good.
        //
        // Now I need two triangles:
        //   Triangle A = (vc, vd, va):
        //     vc→vd (h0), vd→va (???), va→vc (???)
        //     h1n goes a→d, we need d→a. That's opposite(h1n).
        //     Hmm, but we can't use opposite(h1n) because that's in a different edge pair.
        //
        // Wait. We don't change the other 4 halfedges' TO vertices. We only change next/prev
        // pointers and face assignments. Let me reconsider.
        //
        // The 6 halfedges in play are h0, h0n, h0p, h1, h1n, h1p.
        // We set h0.to = vd, h1.to = vc. Everything else stays.
        // h0n.to = vc (still), h0p.to = va (still)
        // h1n.to = vd (still), h1p.to = vb (still)
        //
        // h0: vc → vd
        // h0n: vb → vc
        // h0p: vc → va  (wait, h0p.to = va, h0p.from = ToVertex(opposite(h0p))
        //   Actually: from = FromVertex(h0p) = ToVertex(OppositeHalfedge(h0p))
        //   Before the flip, h0p was c→a, so FromVertex = c, ToVertex = a.
        //   We haven't changed h0p's ToVertex, so h0p is still "?→a".
        //   h0p.from was vc originally. We haven't changed that. So h0p: vc→va.
        // h1: vd → vc
        // h1n: va → vd
        // h1p: vd → vb  (h1p.to = vb, h1p.from = vd? Let's verify:
        //   h1p was d→b before. h1p.to = vb. h1p.from = ToVertex(opposite(h1p)).
        //   We haven't changed that, so h1p: vd→vb.
        //
        // Good. Now I want:
        //   Face 0: triangle (vc, vd, va) using h0(vc→vd), h1n(va→vd... NO: h1n: va→vd)
        //     Hmm, h1n goes va→vd, but I need vd→va.
        //
        // Actually wait. For triangle (vc, vd, va):
        //   Edge vc→vd: h0
        //   Edge vd→va: I need a halfedge from vd to va. That's not any of my 6.
        //   Edge va→vc: I need a halfedge from va to vc. h0n goes vb→vc, not va→vc.
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
        //   New Face 1: h1, h0p, h1n
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
}

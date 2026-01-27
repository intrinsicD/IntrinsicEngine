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
            do
            {
                if (IsBoundary(h))
                {
                    SetHalfedge(v, h);
                    return;
                }
                h = CWRotatedHalfedge(h);
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
            do
            {
                ++count;
                h = CWRotatedHalfedge(h);
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
}

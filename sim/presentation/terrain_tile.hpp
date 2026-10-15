#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/presentation/terrain_relief.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

// Terrain tiles of the continuous view (ADR-0022 §4.8, task R1-04 step 3):
// a quadtree over the six faces of a cube projected onto the sphere.
// A tile is a pure function of its key, the relief and the drawing
// parameters, so the same tile is byte-identical however the camera
// reached it (V10), and neighbouring tiles of any level agree where they
// meet. Units are planet radii, as the planet node draws.

inline constexpr std::uint32_t tile_grid = 32;        // quads per tile edge
inline constexpr std::uint32_t tile_max_level = 18;   // about 2 m vertex spacing on Earth
inline constexpr double tile_skirt_depth = 0.04;      // of the tile's width
inline constexpr double tile_filtered_cells = 0.2;    // vertex spacing, in cells, above which a tile is filtered

struct TileKey {
    std::uint8_t face = 0;   // 0..5: +x, -x, +y, -y, +z, -z
    std::uint8_t level = 0;  // 0 covers a whole face
    std::uint32_t x = 0;     // 0 .. 2^level - 1
    std::uint32_t y = 0;
    friend bool operator==(const TileKey&, const TileKey&) = default;
};

[[nodiscard]] std::array<TileKey, 4> tile_children(const TileKey& key);
[[nodiscard]] TileKey tile_parent(const TileKey& key);
// The direction of a face point, u and v in [-1, 1] (equal-angle mapping).
[[nodiscard]] Vec3d tile_face_direction(std::uint8_t face, double u, double v);
[[nodiscard]] Vec3d tile_center_direction(const TileKey& key);
// The angular width of a tile on the sphere, roughly, in radians.
[[nodiscard]] double tile_angular_size(const TileKey& key);

struct TileDrawing {
    double relief_exaggeration = 25.0;
    double sea_level_m = 0.0;
    double radius_m = 6'371'000.0;
    std::uint32_t texture_width = 2048; // the channel texture's width, for texel addresses
};

// Vertex arrays ready for Godot, as separate triangles: every triangle's
// three vertices carry the same three texel addresses (the fan triangle of
// the cell mesh under the triangle's centroid) and their own barycentric
// weights in it, so the styles read the channels as on the cell mesh.
struct TileGeometry {
    TileKey key;
    Vec3d origin;                    // planet-centred, in radii, double
    std::vector<float> positions;    // xyz relative to origin
    std::vector<float> normals;      // xyz
    std::vector<float> cell_texel;   // UV: xy
    std::vector<float> corner_texel; // UV2: xy
    std::vector<float> custom0;      // second corner texel xy, weights of cell and first corner
    std::vector<float> custom1;      // planet-centred unit direction xyz, height above sea (m)
    double min_radius = 0.0;         // of the drawn surface, for culling and collision
    double max_radius = 0.0;
};

[[nodiscard]] TileGeometry make_terrain_tile(const TileKey& key, const TerrainRelief& relief,
                                             const TileDrawing& drawing);

}  // namespace planetsim::presentation

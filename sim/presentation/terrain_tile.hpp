#pragma once

#include "sim/core/math/vec3d.hpp"
#include "sim/presentation/terrain_relief.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace planetsim::presentation {

struct CoastPage;
class CoastWaterField;

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
    std::vector<float> coast_uv;     // with a coast page: the vertex's place in it, xy in [0, 1]
    TileKey coast_key;               // the page it was made with (its own or an ancestor's)
    double min_radius = 0.0;         // of the drawn surface, for culling and collision
    double max_radius = 0.0;
};

// With a coast page (ADR-0030 A1.4), the tile's own or, while its own
// loads, an ancestor's (A1.3: the finest resident page is drawn as it
// stands), the geometry follows the page's classification: a water vertex
// is drawn at sea level with a height of at most 0, a land vertex at its
// height but no lower than sea level. With its own page every vertex lies
// on a node (every eighth); an ancestor's up to three levels up still has
// a node at every vertex, and its bilinear value is read in between
// otherwise. The ring of points outside the tile, used only for normals,
// reads the page where it covers them and otherwise takes the value the
// neighbouring page of its level holds there (coast_node_value), so shared
// edges keep their normals. `coast_uv` holds the vertices' places in that
// page. Without a page the tile is as before, byte for byte.
[[nodiscard]] TileGeometry make_terrain_tile(const TileKey& key, const TerrainRelief& relief,
                                             const TileDrawing& drawing, const CoastPage* coast = nullptr,
                                             const CoastWaterField* field = nullptr);

}  // namespace planetsim::presentation

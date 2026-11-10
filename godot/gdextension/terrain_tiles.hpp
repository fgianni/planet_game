#pragma once

#include "sim/presentation/coast_page.hpp"
#include "sim/presentation/terrain_tile.hpp"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace planetsim::godot_bridge {

// The streamed terrain tiles of the continuous view (ADR-0022 §4.8, R1-04
// steps 3-4). Each frame update() chooses tiles by screen-space error from
// the camera's double-precision position, inside the view cone and above
// the horizon; draws a tile's parent until all four of its children are
// ready (so nothing is ever missing and nothing overlaps); asks worker
// threads for the missing tiles, coarsest first, dropping requests no
// longer wanted; turns finished tiles into meshes within a time budget;
// keeps them in a bounded cache; and places every drawn tile at
// origin - camera in double precision, under a top-level node so the
// planet node's own 32-bit offset never reaches them.
//
// With a coast water field, every tile is made with its own coast page
// (ADR-0030 Amendment 1): its geometry follows the page, and the page goes
// to a layer of one texture array the styles read (a page that is water or
// land throughout needs no layer). The six roots are made at once without
// pages, so there is always something to draw, and are made again with
// them.
class TerrainTiles {
public:
    struct View {
        double camera_x = 0.0; // planet-centred, in planet radii
        double camera_y = 0.0;
        double camera_z = 0.0;
        double focal_px = 1000.0;          // viewport height / (2 tan(fov / 2))
        double forward_x = 0.0;            // the camera's view direction
        double forward_y = 0.0;
        double forward_z = -1.0;
        double half_diagonal_rad = 0.8;    // half the view cone's angle, corner to corner
        double relief_scale = 1.0;         // drawn over baked relief exaggeration (the shaders rescale heights)
    };

    struct Stats {
        std::int64_t drawn = 0;
        std::int64_t cached = 0;
        std::int64_t meshed = 0;   // turned into meshes this update
        std::int64_t pending = 0;  // wanted, not yet ready
        std::int64_t deepest_level = 0;
        double mesh_ms = 0.0;
        std::vector<std::int64_t> drawn_by_level;
        // V13: the largest screen error, in pixels, that rounding a drawn
        // tile's translation (origin - camera) to 32 bits makes this frame,
        // at the tile's nearest distance from the camera. With the camera at
        // the origin and only rotating, and a tile's vertices fixed relative
        // to it, this is what could make the terrain jitter.
        double translation_error_px = 0.0;
        // Coast pages (ADR-0030 A1): layers in use, and drawn tiles whose
        // page found no free layer (they fall back to the drawn surface).
        std::int64_t coast_layers = 0;
        std::int64_t coast_without_layer = 0;
    };

    TerrainTiles(godot::Node3D* parent, std::shared_ptr<const presentation::TerrainRelief> relief,
                 presentation::TileDrawing drawing, godot::Ref<godot::ShaderMaterial> material,
                 double highest_elevation_m,
                 std::shared_ptr<const presentation::CoastWaterField> coast = nullptr);
    ~TerrainTiles();
    TerrainTiles(const TerrainTiles&) = delete;
    TerrainTiles& operator=(const TerrainTiles&) = delete;

    void update(const View& view);
    void set_visible(bool visible);
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] const presentation::TerrainRelief& relief() const noexcept { return *relief_; }
    [[nodiscard]] const presentation::TileDrawing& drawing() const noexcept { return drawing_; }

    static constexpr double split_error_px = 4.0;   // geometric error on screen that splits a tile
    static constexpr double max_triangle_cells = 0.3; // vertex spacing, in cells, for faithful channels
    static constexpr double mesh_budget_ms = 6.0;   // turning finished tiles into meshes, per update
    static constexpr std::size_t cache_tiles = 512; // about 0.5 MB of vertices each
    // Coast page layers: 128 x 257² binary16 is 16.9 MB, within ADR-0030's
    // 64 MiB of coastal constraints. Most tiles need none: R1-04's flights
    // use at most 40 (L4) and 68 (L6).
    static constexpr int coast_layer_count = 128;

private:
    struct Entry {
        planetsim::Vec3d origin;
        godot::MeshInstance3D* instance = nullptr;
        godot::Ref<godot::ArrayMesh> mesh;
        std::uint64_t last_used = 0;
        double min_radius = 1.0; // of the drawn surface
        double max_radius = 1.0;
        bool with_coast = false; // made with its coast page
        int coast_layer = -1;    // its page's layer, if it has one
        bool coast_missing = false; // its page needed a layer and found none
    };

    struct Made {
        presentation::TileGeometry tile;
        std::optional<presentation::CoastPage> page;
    };

    [[nodiscard]] static std::uint64_t id(const presentation::TileKey& key) noexcept;
    [[nodiscard]] bool wants_split(const presentation::TileKey& key, const Entry& entry, const View& view) const;
    [[nodiscard]] bool culled(const presentation::TileKey& key, const Entry& entry, const View& view) const;
    void select(const presentation::TileKey& key, const View& view);
    void add(const Made& made);
    void release(Entry& entry);
    void evict();
    void work();

    godot::Node3D* root_ = nullptr; // top-level holder of the tile instances
    std::uint64_t root_id_ = 0;     // its ObjectID: the planet node may have freed it first
    std::shared_ptr<const presentation::TerrainRelief> relief_;
    std::shared_ptr<const presentation::CoastWaterField> coast_;
    godot::Ref<godot::Texture2DArray> coast_pages_;
    std::vector<int> free_layers_;
    presentation::TileDrawing drawing_;
    godot::Ref<godot::ShaderMaterial> material_;
    double highest_radius_ = 1.0;
    std::unordered_map<std::uint64_t, Entry> tiles_;
    std::vector<presentation::TileKey> drawn_;
    std::vector<presentation::TileKey> wanted_;
    std::uint64_t frame_ = 0;
    Stats stats_;

    // Worker threads make tiles from `queue_` into `done_`.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<presentation::TileKey> queue_;
    std::unordered_set<std::uint64_t> in_flight_;
    std::vector<Made> done_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace planetsim::godot_bridge

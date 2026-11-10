#include "terrain_tiles.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace planetsim::godot_bridge {
namespace {

[[nodiscard]] godot::Vector3 to_vector(const planetsim::Vec3d& v) {
    return {static_cast<godot::real_t>(v.x), static_cast<godot::real_t>(v.y),
            static_cast<godot::real_t>(v.z)};
}

// A page that is water or land throughout: 1 or -1, else 0 (it needs a
// layer). A point page keeps one sign; a coverage page is fully covered.
[[nodiscard]] float constant_coast(const presentation::CoastPage& page) {
    const bool coverage = page.kind == presentation::CoastPageKind::coverage;
    bool all_water = true;
    bool all_land = true;
    for (std::uint32_t y = 0; y < presentation::coast_page_nodes && (all_water || all_land); ++y) {
        for (std::uint32_t x = 0; x < presentation::coast_page_nodes; ++x) {
            const double v = page.signed_water_m(x, y);
            all_water = all_water && (coverage ? v >= 0.5 : v > 0.0);
            all_land = all_land && (coverage ? v <= -0.5 : v <= 0.0);
        }
    }
    return all_water ? 1.0F : (all_land ? -1.0F : 0.0F);
}

[[nodiscard]] godot::Ref<godot::Image> page_image(const std::vector<std::uint8_t>* bytes) {
    constexpr auto nodes = static_cast<std::int32_t>(presentation::coast_page_nodes);
    godot::PackedByteArray data;
    data.resize(static_cast<std::int64_t>(nodes) * nodes * 2);
    if (bytes != nullptr) {
        std::copy(bytes->begin(), bytes->end(), data.ptrw());
    } else {
        std::fill(data.ptrw(), data.ptrw() + data.size(), std::uint8_t{0});
    }
    return godot::Image::create_from_data(nodes, nodes, false, godot::Image::FORMAT_RH, data);
}

[[nodiscard]] double now_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

TerrainTiles::TerrainTiles(godot::Node3D* parent,
                           std::shared_ptr<const presentation::TerrainRelief> relief,
                           presentation::TileDrawing drawing, godot::Ref<godot::ShaderMaterial> material,
                           double highest_elevation_m,
                           std::shared_ptr<const presentation::CoastWaterField> coast)
    : relief_(std::move(relief)), coast_(std::move(coast)), drawing_(drawing), material_(std::move(material)) {
    if (coast_) {
        godot::TypedArray<godot::Ref<godot::Image>> layers;
        for (int layer = 0; layer < coast_layer_count; ++layer) layers.push_back(page_image(nullptr));
        coast_pages_.instantiate();
        coast_pages_->create_from_images(layers);
        material_->set_shader_parameter("coast_pages", coast_pages_);
        for (int layer = coast_layer_count - 1; layer >= 0; --layer) free_layers_.push_back(layer);
    }
    highest_radius_ = 1.0 + drawing_.relief_exaggeration / drawing_.radius_m *
                                std::max(0.0, highest_elevation_m - drawing_.sea_level_m);
    root_ = memnew(godot::Node3D);
    root_id_ = root_->get_instance_id();
    root_->set_name("TerrainTiles");
    parent->add_child(root_);
    // Placed in world space from double-precision positions, not under the
    // planet node's own 32-bit transform.
    root_->set_as_top_level(true);
    root_->set_transform(godot::Transform3D());
    root_->set_visible(false);
    // The six roots draw the filtered surface and are cheap: made here, so
    // there is always something to draw (without coast pages: those are
    // asked of the workers like any tile).
    for (std::uint8_t face = 0; face < 6U; ++face)
        add(Made{presentation::make_terrain_tile({face, 0U, 0U, 0U}, *relief_, drawing_), std::nullopt});
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency() / 2U);
    for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { work(); });
}

TerrainTiles::~TerrainTiles() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    for (auto& worker : workers_) worker.join();
    // When the planet node itself is freed, Godot frees its children (this
    // holder and the tiles) before this destructor runs.
    if (godot::Object* root = godot::ObjectDB::get_instance(root_id_); root != nullptr)
        godot::Object::cast_to<godot::Node>(root)->queue_free();
}

void TerrainTiles::work() {
    for (;;) {
        presentation::TileKey key;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            key = queue_.front();
            queue_.pop_front();
            in_flight_.insert(id(key));
        }
        Made made;
        if (coast_) {
            made.page = presentation::make_coast_page(key, *coast_);
            made.tile = presentation::make_terrain_tile(key, *relief_, drawing_, &*made.page, coast_.get());
        } else {
            made.tile = presentation::make_terrain_tile(key, *relief_, drawing_);
        }
        const std::lock_guard lock(mutex_);
        in_flight_.erase(id(key));
        done_.push_back(std::move(made));
    }
}

std::uint64_t TerrainTiles::id(const presentation::TileKey& key) noexcept {
    return (static_cast<std::uint64_t>(key.face) << 61U) | (static_cast<std::uint64_t>(key.level) << 56U) |
           (static_cast<std::uint64_t>(key.x) << 28U) | static_cast<std::uint64_t>(key.y);
}

// A drawn radius at the drawn exaggeration: heights scale from sea level.
static double drawn_radius(double baked, const TerrainTiles::View& view) {
    return 1.0 + (baked - 1.0) * view.relief_scale;
}

// By the tile's own drawn surface, from its lowest to its highest point.
bool TerrainTiles::culled(const presentation::TileKey& key, const Entry& entry, const View& view) const {
    const double low = drawn_radius(entry.min_radius, view);
    const double high = drawn_radius(entry.max_radius, view);
    const planetsim::Vec3d camera{view.camera_x, view.camera_y, view.camera_z};
    const planetsim::Vec3d direction = presentation::tile_center_direction(key);
    const double size = presentation::tile_angular_size(key);
    // Beyond the horizon: past what the camera sees of the sphere, plus what
    // the highest ground there can lift into view.
    const double distance_from_centre = planetsim::length(camera);
    if (distance_from_centre > 1.0) {
        const double angle = std::acos(std::clamp(
            planetsim::dot(camera / distance_from_centre, direction), -1.0, 1.0));
        const double reach = std::acos(1.0 / distance_from_centre) +
                             std::acos(1.0 / std::max(high, 1.0));
        if (angle - 0.75 * size > reach) return true;
    }
    // Outside the view cone, by the tile's bounding sphere.
    const planetsim::Vec3d center = direction * (0.5 * (low + high));
    const double radius = 0.75 * size * high + 0.5 * (high - low);
    const planetsim::Vec3d to_tile = center - camera;
    const double distance = planetsim::length(to_tile);
    if (distance <= radius) return false;
    const planetsim::Vec3d forward{view.forward_x, view.forward_y, view.forward_z};
    const double off_axis = std::acos(std::clamp(planetsim::dot(to_tile / distance, forward), -1.0, 1.0));
    return off_axis > view.half_diagonal_rad + std::asin(std::min(1.0, radius / distance));
}

// A tile splits while its triangles are larger than a fraction of a cell
// (each triangle reads the channels of one cell-mesh fan triangle), or while
// its geometric error would show: the smaller of its vertex spacing and its
// own height range (a flat sea needs no more vertices), and at least the
// sag of its grid below the sphere.
bool TerrainTiles::wants_split(const presentation::TileKey& key, const Entry& entry, const View& view) const {
    if (key.level >= presentation::tile_max_level) return false;
    const double size = presentation::tile_angular_size(key);
    const double spacing = size / static_cast<double>(presentation::tile_grid);
    if (spacing > max_triangle_cells * relief_->cell_spacing_rad()) return true;
    const planetsim::Vec3d camera{view.camera_x, view.camera_y, view.camera_z};
    const double low = drawn_radius(entry.min_radius, view);
    const double high = drawn_radius(entry.max_radius, view);
    const planetsim::Vec3d center = presentation::tile_center_direction(key) * high;
    const double distance = std::max(planetsim::length(camera - center) - 0.75 * size, 1e-9);
    const double error = std::max(spacing * spacing / 8.0, std::min(spacing, high - low));
    return error / distance * view.focal_px > split_error_px;
}

void TerrainTiles::select(const presentation::TileKey& key, const View& view) {
    const auto self = tiles_.find(id(key));
    if (self == tiles_.end() || culled(key, self->second, view)) return;
    // Every tile the walk passes through is kept: evicting an ancestor of a
    // drawn tile would collapse its whole branch on the next frame.
    self->second.last_used = frame_;
    // A tile made without its coast page is asked for again, with it; it
    // draws meanwhile.
    if (coast_ && !self->second.with_coast) wanted_.push_back(key);
    if (wants_split(key, self->second, view)) {
        const auto children = presentation::tile_children(key);
        bool ready = true;
        for (const auto& child : children) {
            if (auto found = tiles_.find(id(child)); found != tiles_.end()) {
                found->second.last_used = frame_; // kept while its siblings arrive
            } else {
                ready = false;
                wanted_.push_back(child);
            }
        }
        if (ready) {
            for (const auto& child : children) select(child, view);
            return;
        }
    }
    drawn_.push_back(key);
}

void TerrainTiles::release(Entry& entry) {
    if (entry.instance != nullptr) entry.instance->queue_free();
    entry.instance = nullptr;
    if (entry.coast_layer >= 0) free_layers_.push_back(entry.coast_layer);
    entry.coast_layer = -1;
}

void TerrainTiles::add(const Made& made) {
    const presentation::TileGeometry& tile = made.tile;
    if (auto previous = tiles_.find(id(tile.key)); previous != tiles_.end()) {
        release(previous->second);
        tiles_.erase(previous);
    }
    Entry entry;
    // The tile's coast page: a constant, a layer, or (no layer free, or no
    // page) the drawn surface's waterline. Its layer and mode go to every
    // vertex (coast_pages.gdshaderinc).
    float mode = 0.0F;
    if (made.page) {
        entry.with_coast = true;
        mode = constant_coast(*made.page);
        if (mode == 0.0F) {
            if (free_layers_.empty()) {
                entry.coast_missing = true;
            } else {
                entry.coast_layer = free_layers_.back();
                free_layers_.pop_back();
                coast_pages_->update_layer(page_image(&made.page->signed_water_le), entry.coast_layer);
                mode = made.page->kind == presentation::CoastPageKind::coverage ? 3.0F : 2.0F;
            }
        }
    }
    const std::size_t vertices = tile.positions.size() / 3U;
    godot::PackedFloat32Array custom2;
    custom2.resize(static_cast<std::int64_t>(vertices * 4U));
    float* coast = custom2.ptrw();
    const bool with_uv = tile.coast_uv.size() == vertices * 2U;
    for (std::size_t v = 0; v < vertices; ++v) {
        coast[4 * v] = with_uv ? tile.coast_uv[2 * v] : 0.0F;
        coast[4 * v + 1] = with_uv ? tile.coast_uv[2 * v + 1] : 0.0F;
        coast[4 * v + 2] = static_cast<float>(entry.coast_layer);
        coast[4 * v + 3] = mode;
    }
    godot::PackedVector3Array positions;
    godot::PackedVector3Array normals;
    godot::PackedVector2Array uv;
    godot::PackedVector2Array uv2;
    godot::PackedFloat32Array custom0;
    godot::PackedFloat32Array custom1;
    positions.resize(static_cast<std::int64_t>(vertices));
    normals.resize(static_cast<std::int64_t>(vertices));
    uv.resize(static_cast<std::int64_t>(vertices));
    uv2.resize(static_cast<std::int64_t>(vertices));
    custom0.resize(static_cast<std::int64_t>(vertices * 4U));
    custom1.resize(static_cast<std::int64_t>(vertices * 4U));
    std::copy(tile.custom0.begin(), tile.custom0.end(), custom0.ptrw());
    std::copy(tile.custom1.begin(), tile.custom1.end(), custom1.ptrw());
    // Vector3 and Vector2 are packed floats in a single-precision build: the
    // tile's arrays copy in whole.
    static_assert(sizeof(godot::Vector3) == 3 * sizeof(float) && sizeof(godot::Vector2) == 2 * sizeof(float));
    std::copy(tile.positions.begin(), tile.positions.end(), reinterpret_cast<float*>(positions.ptrw()));
    std::copy(tile.normals.begin(), tile.normals.end(), reinterpret_cast<float*>(normals.ptrw()));
    std::copy(tile.cell_texel.begin(), tile.cell_texel.end(), reinterpret_cast<float*>(uv.ptrw()));
    std::copy(tile.corner_texel.begin(), tile.corner_texel.end(), reinterpret_cast<float*>(uv2.ptrw()));
    godot::Array arrays;
    arrays.resize(godot::Mesh::ARRAY_MAX);
    arrays[godot::Mesh::ARRAY_VERTEX] = positions;
    arrays[godot::Mesh::ARRAY_NORMAL] = normals;
    arrays[godot::Mesh::ARRAY_TEX_UV] = uv;
    arrays[godot::Mesh::ARRAY_TEX_UV2] = uv2;
    arrays[godot::Mesh::ARRAY_CUSTOM0] = custom0;
    arrays[godot::Mesh::ARRAY_CUSTOM1] = custom1;
    arrays[godot::Mesh::ARRAY_CUSTOM2] = custom2;
    const auto rgba_float = static_cast<int64_t>(godot::Mesh::ARRAY_CUSTOM_RGBA_FLOAT);
    const auto format = static_cast<godot::BitField<godot::Mesh::ArrayFormat>>(
        (rgba_float << godot::Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
        (rgba_float << godot::Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT) |
        (rgba_float << godot::Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT));
    entry.origin = tile.origin;
    entry.min_radius = tile.min_radius;
    entry.max_radius = tile.max_radius;
    entry.mesh.instantiate();
    entry.mesh->add_surface_from_arrays(godot::Mesh::PRIMITIVE_TRIANGLES, arrays, godot::Array(),
                                        godot::Dictionary(), format);
    entry.instance = memnew(godot::MeshInstance3D);
    entry.instance->set_mesh(entry.mesh);
    entry.instance->set_material_override(material_);
    // The shaders lower a tile's land towards true scale as the camera
    // descends; Godot culls by the baked mesh's bounds, so they reach down
    // to sea level.
    entry.instance->set_extra_cull_margin(static_cast<float>(std::max(tile.max_radius - 1.0, 0.0)));
    entry.instance->set_visible(false);
    root_->add_child(entry.instance);
    entry.last_used = frame_;
    tiles_.insert_or_assign(id(tile.key), std::move(entry));
}

void TerrainTiles::evict() {
    if (tiles_.size() <= cache_tiles) return;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> candidates; // last used, id
    for (const auto& [key_id, entry] : tiles_) {
        const bool root = ((key_id >> 56U) & 0x1FU) == 0U;
        if (!root && entry.last_used != frame_) candidates.emplace_back(entry.last_used, key_id);
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& [last_used, key_id] : candidates) {
        if (tiles_.size() <= cache_tiles) break;
        auto found = tiles_.find(key_id);
        release(found->second);
        tiles_.erase(found);
    }
}

void TerrainTiles::set_visible(bool visible) {
    root_->set_visible(visible);
}

void TerrainTiles::update(const View& view) {
    ++frame_;
    // Finished tiles become meshes, within the budget; the rest wait.
    std::vector<Made> finished;
    {
        const std::lock_guard lock(mutex_);
        finished.swap(done_);
    }
    const double start = now_ms();
    stats_.meshed = 0;
    std::size_t used = 0;
    for (; used < finished.size() && now_ms() - start < mesh_budget_ms; ++used) {
        // Already there, unless this is a tile's coast page arriving.
        if (auto present = tiles_.find(id(finished[used].tile.key));
            present != tiles_.end() && (present->second.with_coast || !finished[used].page))
            continue;
        add(finished[used]);
        ++stats_.meshed;
    }
    stats_.mesh_ms = now_ms() - start;

    for (const auto& key : drawn_) {
        if (auto found = tiles_.find(id(key)); found != tiles_.end())
            found->second.instance->set_visible(false);
    }
    drawn_.clear();
    wanted_.clear();
    for (std::uint8_t face = 0; face < 6U; ++face) select({face, 0U, 0U, 0U}, view);

    // The workers' queue becomes what is wanted now, coarsest first; tiles
    // finished but not yet meshed, or being made, are not asked for again.
    std::stable_sort(wanted_.begin(), wanted_.end(), [](const auto& a, const auto& b) { return a.level < b.level; });
    std::unordered_set<std::uint64_t> waiting;
    for (std::size_t i = used; i < finished.size(); ++i) waiting.insert(id(finished[i].tile.key));
    {
        const std::lock_guard lock(mutex_);
        queue_.clear();
        for (const auto& key : wanted_) {
            const std::uint64_t key_id = id(key);
            if (!waiting.contains(key_id) && !in_flight_.contains(key_id)) queue_.push_back(key);
        }
        for (std::size_t i = used; i < finished.size(); ++i) done_.push_back(std::move(finished[i]));
    }
    wake_.notify_all();

    // Camera-relative placement, in double.
    stats_.deepest_level = 0;
    stats_.translation_error_px = 0.0;
    stats_.drawn_by_level.assign(presentation::tile_max_level + 1U, 0);
    stats_.coast_without_layer = 0;
    const double camera_radius = std::sqrt(view.camera_x * view.camera_x + view.camera_y * view.camera_y +
                                           view.camera_z * view.camera_z);
    for (const auto& key : drawn_) {
        ++stats_.drawn_by_level[key.level];
        Entry& entry = tiles_.at(id(key));
        entry.last_used = frame_;
        const planetsim::Vec3d offset{entry.origin.x - view.camera_x, entry.origin.y - view.camera_y,
                                      entry.origin.z - view.camera_z};
        const godot::Vector3 sent = to_vector(offset);
        entry.instance->set_position(sent);
        const planetsim::Vec3d rounding{static_cast<double>(sent.x) - offset.x, static_cast<double>(sent.y) - offset.y,
                                        static_cast<double>(sent.z) - offset.z};
        const double nearest = std::max({planetsim::length(offset) - 0.75 * presentation::tile_angular_size(key) * drawn_radius(entry.max_radius, view),
                                         camera_radius - drawn_radius(entry.max_radius, view), 1e-12});
        stats_.translation_error_px =
            std::max(stats_.translation_error_px, planetsim::length(rounding) / nearest * view.focal_px);
        entry.instance->set_visible(true);
        stats_.coast_without_layer += entry.coast_missing ? 1 : 0;
        stats_.deepest_level = std::max<std::int64_t>(stats_.deepest_level, key.level);
    }
    evict();
    stats_.drawn = static_cast<std::int64_t>(drawn_.size());
    stats_.cached = static_cast<std::int64_t>(tiles_.size());
    stats_.pending = static_cast<std::int64_t>(wanted_.size());
    stats_.coast_layers = coast_ ? coast_layer_count - static_cast<std::int64_t>(free_layers_.size()) : 0;
}

}  // namespace planetsim::godot_bridge

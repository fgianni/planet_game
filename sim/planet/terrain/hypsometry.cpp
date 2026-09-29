#include "sim/planet/terrain/hypsometry.hpp"

#include "sim/core/random/noise.hpp"
#include "sim/core/scheduler/deterministic_executor.hpp"
#include "sim/planet/geology/geology_random.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace planetsim {
namespace {

struct Barycentric {
    double center;
    double first_corner;
    double second_corner;
};

constexpr double one_sixth = 1.0 / 6.0;
constexpr double two_thirds = 2.0 / 3.0;
constexpr double five_twelfths = 5.0 / 12.0;

constexpr std::array<Barycentric, hypsometry_points_per_triangle> sample_points{{
    {two_thirds, one_sixth, one_sixth},
    {one_sixth, two_thirds, one_sixth},
    {one_sixth, one_sixth, two_thirds},
    {one_sixth, five_twelfths, five_twelfths},
    {five_twelfths, one_sixth, five_twelfths},
    {five_twelfths, five_twelfths, one_sixth},
}};

// Mean structural elevation of the (three) cells sharing each corner.
[[nodiscard]] std::vector<double> corner_elevations(const PlanetMesh& mesh,
                                                    const Field2D<float>& elevation_m) {
    std::vector<double> sum(mesh.corner_count(), 0.0);
    std::vector<std::uint32_t> count(mesh.corner_count(), 0U);
    for (const auto& cell : mesh.cells()) {
        for (const CornerIndex corner : mesh.cell_corners(cell.id)) {
            sum[corner] += static_cast<double>(elevation_m[cell.id]);
            ++count[corner];
        }
    }
    for (std::size_t corner = 0; corner < sum.size(); ++corner) {
        if (count[corner] != 3U) {
            throw std::logic_error("a dual-mesh corner must be shared by exactly three cells");
        }
        sum[corner] /= 3.0;
    }
    return sum;
}

}  // namespace

HypsometryQuantiles weighted_elevation_quantiles(std::span<ElevationSample> samples) {
    if (samples.empty()) {
        throw std::invalid_argument("hypsometry needs at least one sample");
    }
    // Validate before sorting: a NaN elevation would break the comparator's
    // strict weak ordering, which is undefined behaviour in the sort.
    for (const auto& sample : samples) {
        if (!(sample.weight > 0.0) || !std::isfinite(sample.elevation_m)) {
            throw std::invalid_argument("hypsometry samples need positive weight, finite elevation");
        }
    }
    std::stable_sort(samples.begin(), samples.end(),
                     [](const ElevationSample& left, const ElevationSample& right) {
                         return left.elevation_m < right.elevation_m;
                     });
    double total_weight = 0.0;
    for (const auto& sample : samples) {
        total_weight += sample.weight;
    }

    HypsometryQuantiles quantiles{};
    std::size_t sample = 0;
    double cumulative_before = 0.0;  // weight of samples before `sample`
    double previous_position = 0.0;
    for (std::size_t level = 0; level < hypsometry_quantile_count; ++level) {
        const double fraction =
            static_cast<double>(level) / static_cast<double>(hypsometry_quantile_count - 1U);
        // Advance to the first sample whose midpoint position is >= fraction.
        double position = (cumulative_before + 0.5 * samples[sample].weight) / total_weight;
        while (position < fraction && sample + 1U < samples.size()) {
            previous_position = position;
            cumulative_before += samples[sample].weight;
            ++sample;
            position = (cumulative_before + 0.5 * samples[sample].weight) / total_weight;
        }
        double value = samples[sample].elevation_m;
        if (position > fraction && sample > 0U) {
            const double t = (fraction - previous_position) / (position - previous_position);
            value = samples[sample - 1U].elevation_m +
                    t * (samples[sample].elevation_m - samples[sample - 1U].elevation_m);
        }
        if (level == 0U) {
            value = samples.front().elevation_m;
        } else if (level + 1U == hypsometry_quantile_count) {
            value = samples.back().elevation_m;
        }
        quantiles[level] = static_cast<float>(value);
    }
    return quantiles;
}

double below_fraction(std::span<const float, hypsometry_quantile_count> quantiles,
                      double level_m) noexcept {
    constexpr std::size_t segments = hypsometry_quantile_count - 1U;
    if (level_m <= static_cast<double>(quantiles[0])) {
        return 0.0;
    }
    for (std::size_t segment = 0; segment < segments; ++segment) {
        const double low = quantiles[segment];
        const double high = quantiles[segment + 1U];
        if (level_m <= high) {
            // level_m > low here unless the segment is flat at level_m.
            const double within = high > low ? (level_m - low) / (high - low) : 0.0;
            return (static_cast<double>(segment) + within) / static_cast<double>(segments);
        }
    }
    return 1.0;
}

double mean_elevation_m(std::span<const float, hypsometry_quantile_count> quantiles) noexcept {
    constexpr std::size_t segments = hypsometry_quantile_count - 1U;
    double sum = 0.0;
    for (std::size_t segment = 0; segment < segments; ++segment) {
        sum += 0.5 * (static_cast<double>(quantiles[segment]) +
                      static_cast<double>(quantiles[segment + 1U]));
    }
    return sum / static_cast<double>(segments);
}

double mean_elevation_above_m(
    std::span<const float, hypsometry_quantile_count> quantiles,
    double level_m) noexcept {
    constexpr std::size_t segments = hypsometry_quantile_count - 1U;
    constexpr double segment_width = 1.0 / static_cast<double>(segments);

    const double start_fraction = below_fraction(quantiles, level_m);
    if (start_fraction <= 0.0) {
        return mean_elevation_m(quantiles);
    }
    if (start_fraction >= 1.0) {
        return static_cast<double>(quantiles.back());
    }

    double integral = 0.0;
    for (std::size_t segment = 0; segment < segments; ++segment) {
        const double segment_start = static_cast<double>(segment) * segment_width;
        const double segment_end = segment_start + segment_width;
        const double integration_start = std::max(start_fraction, segment_start);
        if (integration_start >= segment_end) {
            continue;
        }

        const double within = (integration_start - segment_start) / segment_width;
        const double value_at_start =
            static_cast<double>(quantiles[segment]) +
            within * (static_cast<double>(quantiles[segment + 1U]) -
                      static_cast<double>(quantiles[segment]));
        integral += 0.5 * (value_at_start + static_cast<double>(quantiles[segment + 1U])) *
                    (segment_end - integration_start);
    }
    return integral / (1.0 - start_fraction);
}

HypsometryQuantiles cell_hypsometry(const Field3D<float>& hypsometry_m, CellId cell) {
    HypsometryQuantiles quantiles{};
    for (std::size_t level = 0; level < hypsometry_quantile_count; ++level) {
        quantiles[level] = hypsometry_m.at(level, cell);
    }
    return quantiles;
}

double spherical_triangle_area_unit(const Vec3d& first, const Vec3d& second,
                                    const Vec3d& third) noexcept {
    const double numerator = std::abs(dot(first, cross(second, third)));
    const double denominator = 1.0 + dot(first, second) + dot(second, third) + dot(third, first);
    return 2.0 * std::atan2(numerator, denominator);
}

void sample_hypsometry(const PlanetMesh& mesh, std::uint64_t world_seed,
                       const GeologyParameters& parameters, const GeologyState& geology,
                       Field3D<float>& hypsometry_m, std::size_t worker_count) {
    const std::size_t cell_count = mesh.cell_count();
    if (geology.structural_elevation_m.size() != cell_count ||
        geology.crust_type.size() != cell_count) {
        throw std::invalid_argument("hypsometry requires structural elevation and crust type");
    }
    if (hypsometry_m.layer_count() != hypsometry_quantile_count ||
        hypsometry_m.cell_count() != cell_count) {
        throw std::invalid_argument("hypsometry field has the wrong shape");
    }

    const std::vector<double> corner_elevation_m =
        corner_elevations(mesh, geology.structural_elevation_m);
    const std::span<const Vec3d> corners = mesh.corners_unit();
    const std::uint64_t salt = geology_noise_salt(GeologyNoise::subcell_roughness);
    std::array<std::span<float>, hypsometry_quantile_count> layers;
    for (std::size_t level = 0; level < hypsometry_quantile_count; ++level) {
        layers[level] = hypsometry_m.layer(level);
    }

    for_each_deterministic_block(mesh.blocks(), worker_count, [&](std::size_t, const CellBlock& block) {
        std::vector<ElevationSample> samples;
        for (std::uint32_t index = block.begin; index < block.end; ++index) {
            const CellId cell{index};
            const Vec3d center = mesh.cell(cell).center_unit;
            const double center_elevation_m = geology.structural_elevation_m[cell];
            const double roughness_m = geology.crust_type[cell] == CrustType::continental
                                           ? parameters.continental_subcell_roughness_amplitude_m
                                           : parameters.oceanic_subcell_roughness_amplitude_m;
            const std::span<const CornerIndex> cell_corners = mesh.cell_corners(cell);
            samples.clear();
            for (std::size_t k = 0; k < cell_corners.size(); ++k) {
                const CornerIndex first = cell_corners[k];
                const CornerIndex second = cell_corners[(k + 1U) % cell_corners.size()];
                const double weight =
                    spherical_triangle_area_unit(center, corners[first], corners[second]) /
                    static_cast<double>(hypsometry_points_per_triangle);
                for (const auto& point : sample_points) {
                    const Vec3d position =
                        normalized(center * point.center + corners[first] * point.first_corner +
                                   corners[second] * point.second_corner);
                    const double elevation =
                        point.center * center_elevation_m +
                        point.first_corner * corner_elevation_m[first] +
                        point.second_corner * corner_elevation_m[second] +
                        roughness_m * normalized_fbm(world_seed, salt, position,
                                                     parameters.subcell_roughness_noise);
                    samples.push_back({elevation, weight});
                }
            }
            const HypsometryQuantiles quantiles = weighted_elevation_quantiles(samples);
            for (std::size_t level = 0; level < hypsometry_quantile_count; ++level) {
                layers[level][index] = quantiles[level];
            }
        }
    });
}

}  // namespace planetsim

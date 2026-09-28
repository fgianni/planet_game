#include "sim/planet/operators/operator_validation.hpp"

#include "sim/core/fields/field.hpp"
#include "sim/core/math/vec3d.hpp"
#include "sim/core/random/counter_rng.hpp"
#include "sim/planet/operators/finite_volume.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace planetsim {
namespace {

// Rows of a fixed rotation (z-y-z Euler angles 0.4, 1.1, 2.3 rad), chosen to
// share no axis with the icosahedron.
class Rotation {
  public:
    Rotation() {
        const double a = 0.4;
        const double b = 1.1;
        const double c = 2.3;
        const double ca = std::cos(a);
        const double sa = std::sin(a);
        const double cb = std::cos(b);
        const double sb = std::sin(b);
        const double cc = std::cos(c);
        const double sc = std::sin(c);
        rows_ = {{
            {ca * cb * cc - sa * sc, -ca * cb * sc - sa * cc, ca * sb},
            {sa * cb * cc + ca * sc, -sa * cb * sc + ca * cc, sa * sb},
            {-sb * cc, sb * sc, cb},
        }};
    }

    [[nodiscard]] Vec3d apply(const Vec3d& vector) const noexcept {
        return {dot(rows_[0], vector), dot(rows_[1], vector), dot(rows_[2], vector)};
    }

    [[nodiscard]] Vec3d apply_transpose(const Vec3d& vector) const noexcept {
        return rows_[0] * vector.x + rows_[1] * vector.y + rows_[2] * vector.z;
    }

  private:
    std::array<Vec3d, 3> rows_{};
};

struct AnalyticFields {
    Rotation rotation;
    double radius_m = 1.0;

    // Degree-3 and degree-2 harmonic polynomials in the rotated frame.
    [[nodiscard]] static double y3(const Vec3d& p) noexcept { return p.x * p.y * p.z; }
    [[nodiscard]] static double y2(const Vec3d& p) noexcept { return p.x * p.x - p.y * p.y; }

    [[nodiscard]] double scalar(const Vec3d& unit) const noexcept {
        const Vec3d p = rotation.apply(unit);
        return y3(p) + 0.5 * y2(p);
    }

    [[nodiscard]] double scalar_laplacian(const Vec3d& unit) const noexcept {
        const Vec3d p = rotation.apply(unit);
        return -(12.0 * y3(p) + 3.0 * y2(p)) / (radius_m * radius_m);
    }

    [[nodiscard]] Vec3d surface_gradient_of(const Vec3d& unit,
                                            const Vec3d& rotated_gradient) const noexcept {
        const Vec3d ambient = rotation.apply_transpose(rotated_gradient);
        return (ambient - unit * dot(ambient, unit)) / radius_m;
    }

    [[nodiscard]] Vec3d scalar_gradient(const Vec3d& unit) const noexcept {
        const Vec3d p = rotation.apply(unit);
        return surface_gradient_of(unit, {p.y * p.z + p.x, p.x * p.z - p.y, p.x * p.y});
    }

    // Streamfunction h = y z + (x^2 - z^2) / 2 in the rotated frame.
    [[nodiscard]] double streamfunction(const Vec3d& unit) const noexcept {
        const Vec3d p = rotation.apply(unit);
        return p.y * p.z + 0.5 * (p.x * p.x - p.z * p.z);
    }

    [[nodiscard]] Vec3d streamfunction_gradient(const Vec3d& unit) const noexcept {
        const Vec3d p = rotation.apply(unit);
        return surface_gradient_of(unit, {p.x, p.z, p.y - p.z});
    }

    // u = grad(g) + r x grad(h); div(u) = lap(g).
    [[nodiscard]] Vec3d vector(const Vec3d& unit) const noexcept {
        return scalar_gradient(unit) + cross(unit, streamfunction_gradient(unit));
    }
};

// Corner endpoints of each shared edge, ordered counter-clockwise around the
// edge's first cell as seen from outside the sphere.
[[nodiscard]] std::vector<std::pair<CornerIndex, CornerIndex>>
edge_endpoints(const PlanetMesh& mesh) {
    std::vector<std::pair<CornerIndex, CornerIndex>> endpoints(mesh.edge_count());
    for (const auto& cell : mesh.cells()) {
        const auto cell_edges = mesh.cell_edges(cell.id);
        const auto corners = mesh.cell_corners(cell.id);
        for (std::size_t local = 0; local < cell_edges.size(); ++local) {
            if (mesh.edge(cell_edges[local].edge).first_cell == cell.id) {
                endpoints[cell_edges[local].edge.to_index()] = {
                    corners[local], corners[(local + 1U) % corners.size()]};
            }
        }
    }
    return endpoints;
}

enum class CellRegion : std::uint8_t { pentagon, near_pentagon, seam, interior, other };

[[nodiscard]] double angle_between(const Vec3d& first, const Vec3d& second) noexcept {
    return std::acos(std::clamp(dot(first, second), -1.0, 1.0));
}

// Classifies cells for the error-map pattern check relative to the twelve
// pentagons (the icosahedron's vertices) and the 30 arcs joining adjacent ones.
[[nodiscard]] std::vector<CellRegion> classify_cells(const PlanetMesh& mesh) {
    std::vector<Vec3d> pentagons;
    double spacing_sum_rad = 0.0;
    std::size_t spacing_count = 0;
    for (const auto& cell : mesh.cells()) {
        if (cell.is_pentagon()) {
            pentagons.push_back(cell.center_unit);
        }
        for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
            spacing_sum_rad += mesh.edge(cell_edge.edge).centroid_distance_m / mesh.radius_m();
            ++spacing_count;
        }
    }
    const double spacing_rad = spacing_sum_rad / static_cast<double>(spacing_count);

    double adjacent_angle = 10.0;
    for (std::size_t i = 0; i < pentagons.size(); ++i) {
        for (std::size_t j = i + 1U; j < pentagons.size(); ++j) {
            adjacent_angle = std::min(adjacent_angle, angle_between(pentagons[i], pentagons[j]));
        }
    }
    std::vector<std::pair<Vec3d, Vec3d>> seams;
    for (std::size_t i = 0; i < pentagons.size(); ++i) {
        for (std::size_t j = i + 1U; j < pentagons.size(); ++j) {
            if (angle_between(pentagons[i], pentagons[j]) <= adjacent_angle * (1.0 + 1.0e-9)) {
                seams.emplace_back(pentagons[i], pentagons[j]);
            }
        }
    }

    std::vector<CellRegion> regions(mesh.cell_count(), CellRegion::other);
    for (const auto& cell : mesh.cells()) {
        const Vec3d& x = cell.center_unit;
        double pentagon_distance = 10.0;
        for (const auto& pentagon : pentagons) {
            pentagon_distance = std::min(pentagon_distance, angle_between(x, pentagon));
        }
        double seam_distance = 10.0;
        for (const auto& [first, second] : seams) {
            const Vec3d normal = normalized(cross(first, second));
            const Vec3d in_plane = x - normal * dot(x, normal);
            const double span = angle_between(first, second);
            if (length(in_plane) > 0.0) {
                const Vec3d projected = normalized(in_plane);
                if (angle_between(projected, first) <= span &&
                    angle_between(projected, second) <= span) {
                    seam_distance = std::min(seam_distance,
                                             std::asin(std::min(1.0, std::abs(dot(x, normal)))));
                }
            }
        }

        auto& region = regions[cell.id.to_index()];
        if (cell.is_pentagon()) {
            region = CellRegion::pentagon;
        } else if (pentagons.size() == 12U && pentagon_distance <= 8.0 * spacing_rad) {
            region = CellRegion::near_pentagon;
        } else if (seam_distance <= spacing_rad) {
            region = CellRegion::seam;
        } else if (seam_distance > 3.0 * spacing_rad) {
            region = CellRegion::interior;
        }
    }
    return regions;
}

class NormAccumulator {
  public:
    void add(const CellGeometry& cell, CellRegion region, double error, double exact) {
        const double abs_error = std::abs(error);
        max_exact_ = std::max(max_exact_, std::abs(exact));
        if (region == CellRegion::seam) {
            seam_max_error_ = std::max(seam_max_error_, abs_error);
        } else if (region == CellRegion::interior) {
            interior_max_error_ = std::max(interior_max_error_, abs_error);
        }
        if (cell.is_pentagon()) {
            pentagon_max_error_ = std::max(pentagon_max_error_, abs_error);
            return;
        }
        error_squared_ += cell.area_m2 * error * error;
        exact_squared_ += cell.area_m2 * exact * exact;
        max_error_ = std::max(max_error_, abs_error);
    }

    [[nodiscard]] double max_exact() const noexcept { return max_exact_; }

    [[nodiscard]] OperatorErrorNorms norms() const {
        if (!(exact_squared_ > 0.0) || !(max_exact_ > 0.0)) {
            throw std::logic_error("analytic validation field vanishes");
        }
        return {std::sqrt(error_squared_ / exact_squared_), max_error_ / max_exact_,
                pentagon_max_error_ / max_exact_, seam_max_error_ / max_exact_,
                interior_max_error_ / max_exact_};
    }

  private:
    double error_squared_ = 0.0;
    double exact_squared_ = 0.0;
    double max_error_ = 0.0;
    double max_exact_ = 0.0;
    double pentagon_max_error_ = 0.0;
    double seam_max_error_ = 0.0;
    double interior_max_error_ = 0.0;
};

// Solves the discrete Poisson problem lap(u) = rhs with conjugate gradients on
// the symmetric positive semi-definite form M u = -A rhs, where
// (M u)_i = Σ_e (u_i - u_j) l_e / d_e. The right-hand side is projected onto
// the range of M (orthogonal to constants) and the solution is returned with
// zero area-weighted mean.
[[nodiscard]] std::pair<std::vector<double>, std::size_t>
solve_poisson(const PlanetMesh& mesh, const Field2D<double>& rhs) {
    const std::size_t count = mesh.cell_count();
    const auto apply = [&](const std::vector<double>& x, std::vector<double>& y) {
        for (const auto& cell : mesh.cells()) {
            double sum = 0.0;
            for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
                const auto& edge = mesh.edge(cell_edge.edge);
                sum += (x[cell.id.to_index()] - x[cell_edge.neighbor.to_index()]) *
                       edge.length_m / edge.centroid_distance_m;
            }
            y[cell.id.to_index()] = sum;
        }
    };
    const auto inner = [](const std::vector<double>& a, const std::vector<double>& b) {
        double sum = 0.0;
        for (std::size_t index = 0; index < a.size(); ++index) {
            sum += a[index] * b[index];
        }
        return sum;
    };

    std::vector<double> b(count);
    double mean = 0.0;
    for (const auto& cell : mesh.cells()) {
        b[cell.id.to_index()] = -cell.area_m2 * rhs[cell.id];
        mean += b[cell.id.to_index()];
    }
    mean /= static_cast<double>(count);
    for (auto& value : b) {
        value -= mean;
    }

    std::vector<double> x(count, 0.0);
    std::vector<double> r = b;
    std::vector<double> p = r;
    std::vector<double> q(count);
    const double target = 1.0e-24 * inner(b, b);
    double rr = inner(r, r);
    std::size_t iteration = 0;
    for (; iteration < 50'000U && rr > target; ++iteration) {
        apply(p, q);
        const double alpha = rr / inner(p, q);
        for (std::size_t index = 0; index < count; ++index) {
            x[index] += alpha * p[index];
            r[index] -= alpha * q[index];
        }
        const double next = inner(r, r);
        for (std::size_t index = 0; index < count; ++index) {
            p[index] = r[index] + (next / rr) * p[index];
        }
        rr = next;
    }

    double weighted = 0.0;
    double area = 0.0;
    for (const auto& cell : mesh.cells()) {
        weighted += cell.area_m2 * x[cell.id.to_index()];
        area += cell.area_m2;
    }
    for (auto& value : x) {
        value -= weighted / area;
    }
    return {std::move(x), iteration};
}

}  // namespace

OperatorValidation validate_operators(const PlanetMesh& mesh, std::size_t worker_count) {
    const std::size_t cell_count = mesh.cell_count();
    AnalyticFields analytic;
    analytic.radius_m = mesh.radius_m();

    Field2D<double> scalar(cell_count);
    for (const auto& cell : mesh.cells()) {
        scalar[cell.id] = analytic.scalar(cell.center_unit);
    }

    const auto corners = mesh.corners_unit();
    const auto endpoints = edge_endpoints(mesh);
    EdgeField<double> exact_edge_flux(mesh.edge_count());
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
        const auto& edge = mesh.edge(edge_id);
        const Vec3d& first = corners[endpoints[index].first];
        const Vec3d& second = corners[endpoints[index].second];
        Vec3d normal = normalized(cross(first, second));
        if (dot(normal, mesh.cell(edge.first_cell).center_unit) > 0.0) {
            normal = normal * -1.0;
        }
        exact_edge_flux[edge_id] = dot(analytic.vector(normalized(first + second)), normal);
    }

    Field2D<double> gradient_east(cell_count);
    Field2D<double> gradient_north(cell_count);
    Field2D<double> flux_divergence(cell_count);
    Field2D<double> scalar_laplacian(cell_count);
    Field2D<double> exact_laplacian(cell_count);
    gradient(mesh, scalar, gradient_east, gradient_north, worker_count);
    divergence(mesh, exact_edge_flux, flux_divergence, worker_count);
    laplacian(mesh, scalar, scalar_laplacian, worker_count);

    double scalar_mean = 0.0;
    double total_area = 0.0;
    for (const auto& cell : mesh.cells()) {
        exact_laplacian[cell.id] = analytic.scalar_laplacian(cell.center_unit);
        scalar_mean += cell.area_m2 * scalar[cell.id];
        total_area += cell.area_m2;
    }
    scalar_mean /= total_area;
    const auto [poisson, poisson_iterations] = solve_poisson(mesh, exact_laplacian);

    const auto regions = classify_cells(mesh);
    NormAccumulator gradient_norms;
    NormAccumulator divergence_norms;
    NormAccumulator laplacian_norms;
    NormAccumulator poisson_norms;
    std::vector<CellOperatorError> errors(cell_count);
    for (const auto& cell : mesh.cells()) {
        const Vec3d exact_gradient = analytic.scalar_gradient(cell.center_unit);
        const double lap = exact_laplacian[cell.id];
        const double centered_scalar = scalar[cell.id] - scalar_mean;

        auto& error = errors[cell.id.to_index()];
        error.gradient =
            std::hypot(gradient_east[cell.id] - dot(exact_gradient, cell.east_unit),
                       gradient_north[cell.id] - dot(exact_gradient, cell.north_unit));
        error.divergence = flux_divergence[cell.id] - lap;
        error.laplacian = scalar_laplacian[cell.id] - lap;
        error.poisson_solution = poisson[cell.id.to_index()] - centered_scalar;

        const CellRegion region = regions[cell.id.to_index()];
        gradient_norms.add(cell, region, error.gradient, length(exact_gradient));
        divergence_norms.add(cell, region, error.divergence, lap);
        laplacian_norms.add(cell, region, error.laplacian, lap);
        poisson_norms.add(cell, region, error.poisson_solution, centered_scalar);
    }
    for (auto& error : errors) {
        error.gradient /= gradient_norms.max_exact();
        error.divergence /= divergence_norms.max_exact();
        error.laplacian /= laplacian_norms.max_exact();
        error.poisson_solution /= poisson_norms.max_exact();
    }

    OperatorValidation result;
    result.cell_count = cell_count;
    result.gradient = gradient_norms.norms();
    result.divergence = divergence_norms.norms();
    result.laplacian = laplacian_norms.norms();
    result.poisson_solution = poisson_norms.norms();
    result.poisson_iterations = poisson_iterations;
    result.cells = std::move(errors);
    return result;
}

NondivergentFluxCheck check_nondivergent_flux(const PlanetMesh& mesh, std::size_t worker_count) {
    AnalyticFields analytic;
    analytic.radius_m = mesh.radius_m();
    const auto corners = mesh.corners_unit();
    const auto endpoints = edge_endpoints(mesh);

    // The outward flux of r x grad(h) through an edge traversed from corner a
    // to corner b counter-clockwise is h(a) - h(b); summed around a cell it
    // telescopes to zero.
    EdgeField<double> streamfunction_flux(mesh.edge_count());
    EdgeField<double> random_flux(mesh.edge_count());
    for (std::size_t index = 0; index < mesh.edge_count(); ++index) {
        const EdgeId edge_id{static_cast<EdgeId::value_type>(index)};
        const double integrated =
            analytic.streamfunction(corners[endpoints[index].first]) -
            analytic.streamfunction(corners[endpoints[index].second]);
        streamfunction_flux[edge_id] = integrated / mesh.edge(edge_id).length_m;
        random_flux[edge_id] =
            keyed_random_unit_double(0x5eedU, RandomStreamId::validation, 0,
                                     static_cast<std::uint32_t>(index)) -
            0.5;
    }

    Field2D<double> streamfunction_divergence(mesh.cell_count());
    Field2D<double> random_divergence(mesh.cell_count());
    divergence(mesh, streamfunction_flux, streamfunction_divergence, worker_count);
    divergence(mesh, random_flux, random_divergence, worker_count);

    double max_divergence = 0.0;
    double max_scale = 0.0;
    double net = 0.0;
    double net_compensation = 0.0;
    double gross = 0.0;
    for (const auto& cell : mesh.cells()) {
        double scale = 0.0;
        for (const auto& cell_edge : mesh.cell_edges(cell.id)) {
            scale += std::abs(streamfunction_flux[cell_edge.edge]) *
                     mesh.edge(cell_edge.edge).length_m;
        }
        max_scale = std::max(max_scale, scale / cell.area_m2);
        max_divergence = std::max(max_divergence, std::abs(streamfunction_divergence[cell.id]));

        const double integral = random_divergence[cell.id] * cell.area_m2;
        const double corrected = integral - net_compensation;
        const double next = net + corrected;
        net_compensation = (next - net) - corrected;
        net = next;
        gross += std::abs(integral);
    }

    return {max_divergence / max_scale, std::abs(net) / gross};
}

}  // namespace planetsim

#include "sim/planet/dynamics/zonal_circulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace planetsim {
namespace {

constexpr double pi = std::numbers::pi_v<double>;
constexpr double seconds_per_day = 86'400.0;

// Forward-mode differentiation in one direction: the residual is written
// once, as a template, and evaluated on doubles or on these.
struct Dual {
    double v = 0.0;
    double d = 0.0;
    constexpr Dual() = default;
    constexpr Dual(double value) : v(value) {}   // NOLINT(google-explicit-constructor)
    constexpr Dual(double value, double derivative) : v(value), d(derivative) {}
};

[[nodiscard]] constexpr Dual operator+(Dual a, Dual b) { return {a.v + b.v, a.d + b.d}; }
[[nodiscard]] constexpr Dual operator-(Dual a, Dual b) { return {a.v - b.v, a.d - b.d}; }
[[nodiscard]] constexpr Dual operator-(Dual a) { return {-a.v, -a.d}; }
[[nodiscard]] constexpr Dual operator*(Dual a, Dual b) {
    return {a.v * b.v, a.d * b.v + a.v * b.d};
}
[[nodiscard]] constexpr Dual operator/(Dual a, Dual b) {
    return {a.v / b.v, (a.d * b.v - a.v * b.d) / (b.v * b.v)};
}
Dual& operator+=(Dual& a, Dual b) { return a = a + b; }
Dual& operator-=(Dual& a, Dual b) { return a = a - b; }
[[nodiscard]] Dual sqrt(Dual a) {
    const double s = std::sqrt(a.v);
    return {s, a.d / (2.0 * s)};
}
[[nodiscard]] Dual log(Dual a) { return {std::log(a.v), a.d / a.v}; }
[[nodiscard]] constexpr double value(double a) { return a; }
[[nodiscard]] constexpr double value(Dual a) { return a.v; }
// max(a, floor), a kink the residual tolerates where it is inactive.
template <class S>
[[nodiscard]] S at_least(S a, double floor) {
    return value(a) > floor ? a : S(floor);
}
using std::log;
using std::sqrt;

// Upwind flux F X_up = ½ F (X_a + X_b) − ½ |F| (X_b − X_a), a on the side
// F > 0 comes from, with |F| smoothed to F² / √(F² + ε²): differentiable,
// zero with the flow (so no diffusion at rest), and upwind once |F| ≫ ε.
template <class S>
[[nodiscard]] S upwind_flux(S flux, S from_a, S from_b, double epsilon) {
    const S magnitude = flux * flux / sqrt(flux * flux + epsilon * epsilon);
    return 0.5 * flux * (from_a + from_b) - 0.5 * magnitude * (from_b - from_a);
}

// van Albada's limited slope from the differences on either side, with ε
// keeping it smooth: ≈ b where the profile is smooth (a ≈ b), ≈ 0 next to
// an extremum. Differentiable, unlike minmod, as Newton needs.
template <class S>
[[nodiscard]] S limited_slope(S a, S b, double epsilon) {
    const double e2 = epsilon * epsilon;
    return (a * (b * b + e2) + b * (a * a + e2)) / (a * a + b * b + 2.0 * e2);
}

// The meridional flux of X through boundary i (between bands i and i + 1)
// from limited MUSCL face values on either side: second order where X is
// smooth, upwind (no new extrema) at extrema and at the poles.
template <class S>
[[nodiscard]] S limited_flux(S flux, std::span<const S> x, std::size_t i, double epsilon,
                             double flux_epsilon) {
    const std::size_t nb = x.size();
    const S jump = x[i + 1U] - x[i];
    const S south_slope = i > 0U ? limited_slope(x[i] - x[i - 1U], jump, epsilon) : S(0.0);
    const S north_slope =
        i + 2U < nb ? limited_slope(jump, x[i + 2U] - x[i + 1U], epsilon) : S(0.0);
    return upwind_flux(flux, x[i] + 0.5 * south_slope, x[i + 1U] - 0.5 * north_slope,
                       flux_epsilon);
}

// The squared eddy (or gust) speed, smoothly 2E for E ≫ w² and w² at E = 0.
template <class S>
[[nodiscard]] S eddy_speed_sq(S energy, double w2) {
    return energy + sqrt(energy * energy + w2 * w2);
}

// ln(y/x) / (y − x), with its series where x and y are close.
[[nodiscard]] double inverse_log_mean(double x, double y) noexcept {
    const double r = y / x - 1.0;
    if (std::abs(r) < 1.0e-4) {
        return (1.0 - r / 2.0 + r * r / 3.0) / x;
    }
    return std::log1p(r) / (y - x);
}

// Interface and layer-mean Exner functions of N equal-mass σ layers under
// p_s, as the reference core computes them.
void layer_exner(double ps, std::size_t n, double p0, double kappa,
                 std::vector<double>& interface_exner, std::vector<double>& mean_exner) {
    const double n_d = static_cast<double>(n);
    interface_exner.resize(n + 1U);
    mean_exner.resize(n);
    for (std::size_t m = 0; m <= n; ++m) {
        const double p = ps * (1.0 - static_cast<double>(m) / n_d);
        interface_exner[m] = m == n ? 0.0 : std::pow(p / p0, kappa);
    }
    for (std::size_t k = 0; k < n; ++k) {
        const double p_bottom = ps * (1.0 - static_cast<double>(k) / n_d);
        const double p_top = ps * (1.0 - static_cast<double>(k + 1U) / n_d);
        mean_exner[k] = (p_bottom * interface_exner[k] - p_top * interface_exner[k + 1U]) /
                        ((1.0 + kappa) * (p_bottom - p_top));
    }
}

// Everything the residual needs that does not depend on the unknowns.
struct Setup {
    std::size_t bands = 0;
    std::size_t layers = 0;
    std::size_t block = 0;
    double a = 0.0;
    double g = 0.0;
    double gas = 0.0;
    double cp = 0.0;
    double omega = 0.0;
    double dphi = 0.0;
    double dy = 0.0;
    // Bands.
    std::vector<double> latitude, cos_band, area;   // area: sin φ_{j+½} − sin φ_{j−½}
    std::vector<double> mass;                       // μ = p_s / (g N)
    std::vector<double> surface_geopotential;
    std::vector<double> exner_interface;            // (N + 1) × band
    std::vector<double> exner_mean;                 // N × band
    std::vector<double> log_pressure;               // N × band, ln p at the layer's mean Exner
    std::vector<double> t_ref, q_ref, lambda, friction;   // N × band
    std::vector<double> drag_band;                  // g N C_D / R
    // Boundaries.
    std::vector<double> latitude_b, cos_b, f_b, tan_b, mass_b;
    std::vector<double> theta_hat_factor;           // N × boundary: ln(π̄_b/π̄_a)/(π̄_b − π̄_a)
    std::vector<double> friction_b;                 // N × boundary
    std::vector<double> drag_b;                     // g N C_D / R
    // Layers.
    std::vector<double> heat_shape;                 // s_k
    std::vector<double> momentum_weight;            // N / n_free in σ < σ_free
    std::vector<double> convective_exner;           // σ_k^κ_c; empty: no convection

    [[nodiscard]] std::size_t u(std::size_t k, std::size_t j) const { return j * block + k; }
    [[nodiscard]] std::size_t theta(std::size_t k, std::size_t j) const {
        return j * block + layers + k;
    }
    [[nodiscard]] std::size_t energy(std::size_t j) const { return j * block + 2U * layers; }
    [[nodiscard]] std::size_t v(std::size_t k, std::size_t i) const {
        return i * block + 2U * layers + 1U + k;
    }
    [[nodiscard]] std::size_t lid(std::size_t i) const { return i * block + 3U * layers + 1U; }
};

Setup make_setup(const ZonalCirculationParameters& p, const ZonalForcing& forcing) {
    Setup s;
    const std::size_t nb = p.bands;
    const std::size_t n = p.layer_count;
    const std::size_t nbd = nb - 1U;
    s.bands = nb;
    s.layers = n;
    s.block = 3U * n + 2U;
    s.a = p.radius_m;
    s.g = p.gravity_m_s2;
    s.gas = p.gas_constant_J_kg_K;
    s.cp = p.heat_capacity_J_kg_K;
    s.omega = p.rotation_rate_rad_s;
    s.dphi = pi / static_cast<double>(nb);
    s.dy = s.a * s.dphi;
    const double kappa = s.gas / s.cp;
    const double n_d = static_cast<double>(n);

    s.latitude.resize(nb);
    s.cos_band.resize(nb);
    s.area.resize(nb);
    s.mass.resize(nb);
    s.surface_geopotential.resize(nb);
    s.exner_interface.resize((n + 1U) * nb);
    s.exner_mean.resize(n * nb);
    s.log_pressure.resize(n * nb);
    s.drag_band.resize(nb);
    std::vector<double> interface_exner;
    std::vector<double> mean_exner;
    for (std::size_t j = 0; j < nb; ++j) {
        const double south = -0.5 * pi + s.dphi * static_cast<double>(j);
        const double north = south + s.dphi;
        s.latitude[j] = 0.5 * (south + north);
        s.cos_band[j] = std::cos(s.latitude[j]);
        s.area[j] = std::sin(north) - std::sin(south);
        const double ps = forcing.surface_pressure_Pa[j];
        s.mass[j] = ps / (s.g * n_d);
        s.surface_geopotential[j] = s.g * forcing.surface_height_m[j];
        layer_exner(ps, n, p.reference_pressure_Pa, kappa, interface_exner, mean_exner);
        for (std::size_t m = 0; m <= n; ++m) {
            s.exner_interface[m * nb + j] = interface_exner[m];
        }
        for (std::size_t k = 0; k < n; ++k) {
            s.exner_mean[k * nb + j] = mean_exner[k];
            s.log_pressure[k * nb + j] =
                std::log(p.reference_pressure_Pa) + std::log(mean_exner[k]) / kappa;
        }
        s.drag_band[j] = s.g * n_d * forcing.drag_coefficient[j] / s.gas;
    }
    s.t_ref = forcing.temperature_K;
    s.q_ref = forcing.heating_K_s;
    s.lambda = forcing.heating_derivative_s;
    s.friction = forcing.rayleigh_friction_s;

    s.latitude_b.resize(nbd);
    s.cos_b.resize(nbd);
    s.f_b.resize(nbd);
    s.tan_b.resize(nbd);
    s.mass_b.resize(nbd);
    s.theta_hat_factor.resize(n * nbd);
    s.friction_b.resize(n * nbd);
    s.drag_b.resize(nbd);
    for (std::size_t i = 0; i < nbd; ++i) {
        const double phi = -0.5 * pi + s.dphi * static_cast<double>(i + 1U);
        s.latitude_b[i] = phi;
        s.cos_b[i] = std::cos(phi);
        s.f_b[i] = 2.0 * s.omega * std::sin(phi);
        s.tan_b[i] = std::tan(phi);
        s.mass_b[i] = 0.5 * (s.mass[i] + s.mass[i + 1U]);
        for (std::size_t k = 0; k < n; ++k) {
            s.theta_hat_factor[k * nbd + i] =
                inverse_log_mean(s.exner_mean[k * nb + i], s.exner_mean[k * nb + i + 1U]);
            s.friction_b[k * nbd + i] =
                0.5 * (s.friction[k * nb + i] + s.friction[k * nb + i + 1U]);
        }
        s.drag_b[i] = 0.5 * (s.drag_band[i] + s.drag_band[i + 1U]);
    }

    if (p.critical_lapse_rate_K_m > 0.0) {
        const double kappa_c = p.gas_constant_J_kg_K * p.critical_lapse_rate_K_m / p.gravity_m_s2;
        s.convective_exner.resize(n);
        for (std::size_t k = 0; k < n; ++k) {
            s.convective_exner[k] = std::pow(1.0 - (static_cast<double>(k) + 0.5) / n_d, kappa_c);
        }
    }
    s.heat_shape.resize(n);
    s.momentum_weight.assign(n, 0.0);
    double mean_sq = 0.0;
    std::size_t free = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double sigma = 1.0 - (static_cast<double>(k) + 0.5) / n_d;
        s.heat_shape[k] = sigma * sigma;
        mean_sq += sigma * sigma / n_d;
        if (sigma < p.free_troposphere_sigma) {
            ++free;
        }
    }
    for (std::size_t k = 0; k < n; ++k) {
        const double sigma = 1.0 - (static_cast<double>(k) + 0.5) / n_d;
        s.heat_shape[k] /= mean_sq;
        if (sigma < p.free_troposphere_sigma) {
            s.momentum_weight[k] = n_d / static_cast<double>(free);
        }
    }
    return s;
}

// The steady equations' residual: tendencies for ū (m/s²) and θ̄ (K/s),
// the screened E equation (m²/s²), the meridional balance (m/s²) and the
// lid (m/s).
template <class S>
void evaluate(const Setup& s, const ZonalCirculationParameters& p, std::span<const S> x,
              std::span<S> r) {
    const std::size_t nb = s.bands;
    const std::size_t nbd = nb - 1U;
    const std::size_t n = s.layers;
    const double a = s.a;
    const double dy = s.dy;
    const double w2 = p.minimum_eddy_velocity_m_s * p.minimum_eddy_velocity_m_s;
    const double n_d = static_cast<double>(n);
    const double v_eps = p.upwind_smoothing_m_s;

    std::vector<S> u(n * nb), th(n * nb), t(n * nb), geo(n * nb), angular(n * nb), e(nb);
    std::vector<S> v(n * nbd), lid(nbd);
    for (std::size_t j = 0; j < nb; ++j) {
        e[j] = x[s.energy(j)];
        S phi = s.surface_geopotential[j];
        for (std::size_t k = 0; k < n; ++k) {
            u[k * nb + j] = x[s.u(k, j)];
            th[k * nb + j] = x[s.theta(k, j)];
            const double pi_bottom = s.exner_interface[k * nb + j];
            const double pi_top = s.exner_interface[(k + 1U) * nb + j];
            const double pi_mean = s.exner_mean[k * nb + j];
            t[k * nb + j] = th[k * nb + j] * pi_mean;
            geo[k * nb + j] = phi + s.cp * th[k * nb + j] * (pi_bottom - pi_mean);
            phi += s.cp * th[k * nb + j] * (pi_bottom - pi_top);
            angular[k * nb + j] =
                (s.omega * a * s.cos_band[j] + u[k * nb + j]) * (a * s.cos_band[j]);
        }
    }
    for (std::size_t i = 0; i < nbd; ++i) {
        lid[i] = x[s.lid(i)];
        for (std::size_t k = 0; k < n; ++k) {
            v[k * nbd + i] = x[s.v(k, i)];
        }
    }

    // Mass fluxes, their divergence, and the upward mass flux W_m at the
    // interfaces from steady continuity (W_0 = 0).
    std::vector<S> flux(n * nbd), divergence(n * nb), vertical((n + 1U) * nb, S(0.0));
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < nbd; ++i) {
            flux[k * nbd + i] = s.mass_b[i] * v[k * nbd + i];
        }
        for (std::size_t j = 0; j < nb; ++j) {
            S net = 0.0;
            if (j + 1U < nb) {
                net += s.cos_b[j] * flux[k * nbd + j];
            }
            if (j > 0U) {
                net -= s.cos_b[j - 1U] * flux[k * nbd + j - 1U];
            }
            divergence[k * nb + j] = net / (a * s.area[j]);
        }
    }
    for (std::size_t j = 0; j < nb; ++j) {
        for (std::size_t k = 0; k + 1U < n; ++k) {
            vertical[(k + 1U) * nb + j] = vertical[k * nb + j] - divergence[k * nb + j];
        }
    }

    // The eddy closure at the boundaries.
    // ∂θ/∂y on pressure surfaces, not σ surfaces, which slope with the
    // terrain: ∂lnθ/∂y|_p = ∂lnθ/∂y|_σ − (∂lnθ/∂ln p) ∂ln p/∂y|_σ, exact for
    // an isothermal column (ln θ linear in ln p).
    std::vector<S> log_theta(n * nb), log_slope(n * nb);
    for (std::size_t c = 0; c < n * nb; ++c) {
        log_theta[c] = log(th[c]);
    }
    for (std::size_t j = 0; j < nb; ++j) {
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t below = (k > 0U ? k - 1U : k) * nb + j;
            const std::size_t above = (k + 1U < n ? k + 1U : k) * nb + j;
            log_slope[k * nb + j] = (log_theta[above] - log_theta[below]) /
                                    (s.log_pressure[above] - s.log_pressure[below]);
        }
    }
    std::vector<S> grad_theta(n * nbd), diffusivity(n * nbd, S(0.0));
    std::vector<S> momentum_flux(nbd, S(0.0)), generation(nbd, S(0.0)), e_b(nbd);
    for (std::size_t i = 0; i < nbd; ++i) {
        S column = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t south = k * nb + i;
            const std::size_t north = south + 1U;
            const S slope = 0.5 * (log_slope[south] + log_slope[north]);
            grad_theta[k * nbd + i] =
                0.5 * (th[south] + th[north]) *
                ((log_theta[north] - log_theta[south]) -
                 slope * (s.log_pressure[north] - s.log_pressure[south])) /
                dy;
            column += grad_theta[k * nbd + i] / n_d;
        }
        e_b[i] = 0.5 * (e[i] + e[i + 1U]);
        if (!p.eddies) {
            continue;
        }
        // χ = L² f² / (L² f² + N_B² H²), N_B from the bottom and top layers.
        const std::size_t top = n - 1U;
        const S theta_bottom = 0.5 * (th[i] + th[i + 1U]);
        const S theta_top = 0.5 * (th[top * nb + i] + th[top * nb + i + 1U]);
        S t_column = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            t_column += 0.5 * (t[k * nb + i] + t[k * nb + i + 1U]) / n_d;
        }
        const S scale_height = s.gas * t_column / s.g;
        const double log_span =
            0.5 * (s.log_pressure[i] + s.log_pressure[i + 1U] - s.log_pressure[top * nb + i] -
                   s.log_pressure[top * nb + i + 1U]);
        const S theta_mean = 0.5 * (theta_bottom + theta_top);
        const S buoyancy_sq =
            at_least(s.g * (theta_top - theta_bottom) / (theta_mean * scale_height * log_span),
                     p.minimum_buoyancy_frequency_sq_s2);
        const double lf2 = p.eddy_scale_m * p.eddy_scale_m * s.f_b[i] * s.f_b[i];
        const S chi = lf2 / (lf2 + buoyancy_sq * scale_height * scale_height);
        generation[i] = chi * column * column;
        momentum_flux[i] = p.momentum_mixing_length_m * (e[i + 1U] - e[i]) / dy;
        const S eddy_speed = sqrt(eddy_speed_sq(e_b[i], w2));
        for (std::size_t k = 0; k < n; ++k) {
            diffusivity[k * nbd + i] = p.heat_mixing_length_m * eddy_speed * s.heat_shape[k];
        }
    }

    // Horizontal fluxes of angular momentum and θ per layer and boundary.
    std::vector<S> m_flux(n * nbd), th_flux(n * nbd);
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < nbd; ++i) {
            const std::size_t south = k * nb + i;
            const std::size_t north = south + 1U;
            const S f = flux[k * nbd + i];
            const double epsilon = s.mass_b[i] * v_eps;
            const S viscous = p.viscosity_m2_s * s.cos_b[i] * s.cos_b[i] *
                              (u[north] / s.cos_band[i + 1U] - u[south] / s.cos_band[i]) /
                              s.dphi;
            const std::span<const S> m_layer(angular.data() + k * nb, nb);
            const std::span<const S> th_layer(th.data() + k * nb, nb);
            m_flux[k * nbd + i] =
                limited_flux(f, m_layer, i, p.limiter_angular_momentum_m2_s, epsilon) +
                s.mass_b[i] * (s.momentum_weight[k] * momentum_flux[i] * a * s.cos_b[i] - viscous);
            th_flux[k * nbd + i] =
                limited_flux(f, th_layer, i, p.limiter_temperature_K, epsilon) -
                s.mass_b[i] * (diffusivity[k * nbd + i] + p.viscosity_m2_s) *
                    grad_theta[k * nbd + i];
        }
    }

    // ū and θ̄ rows.
    for (std::size_t j = 0; j < nb; ++j) {
        const double column_scale = 1.0 / (a * s.area[j]);
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * nb + j;
            S m_div = 0.0;
            S th_div = 0.0;
            if (j + 1U < nb) {
                m_div += s.cos_b[j] * m_flux[k * nbd + j];
                th_div += s.cos_b[j] * th_flux[k * nbd + j];
            }
            if (j > 0U) {
                m_div -= s.cos_b[j - 1U] * m_flux[k * nbd + j - 1U];
                th_div -= s.cos_b[j - 1U] * th_flux[k * nbd + j - 1U];
            }
            S m_tendency = -m_div * column_scale;
            S th_tendency = -th_div * column_scale;
            // Upwind vertical fluxes through the layer's top and bottom.
            for (const std::size_t m : {k, k + 1U}) {
                if (m == 0U || m == n) {
                    continue;
                }
                const S w = vertical[m * nb + j];
                const std::size_t below = (m - 1U) * nb + j;
                const std::size_t above = m * nb + j;
                const double epsilon = s.mass[j] * v_eps / s.dy;
                const double sign = m == k ? 1.0 : -1.0;   // in at the bottom, out at the top
                m_tendency += sign * upwind_flux(w, angular[below], angular[above], epsilon);
                th_tendency += sign * upwind_flux(w, th[below], th[above], epsilon);
            }
            S du = m_tendency / (s.mass[j] * a * s.cos_band[j]) - s.friction[c] * u[c];
            if (k == 0U && s.drag_band[j] > 0.0) {
                const S speed = sqrt(u[c] * u[c] + eddy_speed_sq(e[j], w2));
                du -= s.drag_band[j] / t[c] * speed * u[c];
            }
            r[s.u(k, j)] = du;
            r[s.theta(k, j)] = th_tendency / s.mass[j] +
                               (s.q_ref[c] + s.lambda[c] * (t[c] - s.t_ref[c])) /
                                   s.exner_mean[c];
        }
        // Vertical momentum diffusion between adjacent equal-mass layers,
        // K_v (u_m − u_{m−1}) / Δz², Δz = (R T̄ / g) ln(p_{m−1} / p_m): it
        // conserves the column's angular momentum and ties the winds aloft to
        // the surface drag, which removes the steady problem's null space
        // (any solid-body wind aloft where nothing moves).
        if (p.vertical_viscosity_m2_s > 0.0) {
            for (std::size_t m = 1; m < n; ++m) {
                const std::size_t below = (m - 1U) * nb + j;
                const std::size_t above = m * nb + j;
                const S thickness = s.gas * 0.5 * (t[below] + t[above]) / s.g *
                                    (s.log_pressure[below] - s.log_pressure[above]);
                const S exchange =
                    p.vertical_viscosity_m2_s * (u[above] - u[below]) / (thickness * thickness);
                r[s.u(m - 1U, j)] += exchange;
                r[s.u(m, j)] -= exchange;
            }
        }
        // Convective relaxation: where θ_c = T / σ^κ_c falls upward across an
        // interface, the exchange δ that would make the two layers neutral,
        // δ = (T_b π_a − T_a π_b) / (π_a + π_b), moves up at the rate
        // ρ(δ) / τ_c, conserving their enthalpy. ρ(δ) = δ² / √(δ² + δ₀²) for
        // δ > 0 and 0 otherwise keeps the residual differentiable.
        if (!s.convective_exner.empty()) {
            const double d0 = p.convective_smoothing_K;
            for (std::size_t m = 1; m < n; ++m) {
                const std::size_t below = (m - 1U) * nb + j;
                const std::size_t above = m * nb + j;
                const double pi_b = s.convective_exner[m - 1U];
                const double pi_a = s.convective_exner[m];
                const S excess = (t[below] * pi_a - t[above] * pi_b) / (pi_a + pi_b);
                if (value(excess) <= 0.0) {
                    continue;
                }
                const S rate =
                    excess * excess / sqrt(excess * excess + d0 * d0) / p.convective_time_s;
                r[s.theta(m - 1U, j)] -= rate / s.exner_mean[below];
                r[s.theta(m, j)] += rate / s.exner_mean[above];
            }
        }
        // (1 − L²∇²) E = c_E G, no flux through the poles.
        S laplacian = 0.0;
        if (j + 1U < nb) {
            laplacian += s.cos_b[j] * (e[j + 1U] - e[j]);
        }
        if (j > 0U) {
            laplacian -= s.cos_b[j - 1U] * (e[j] - e[j - 1U]);
        }
        laplacian = laplacian / (dy * a * s.area[j]);
        S source = 0.0;
        if (j == 0U) {
            source = generation[0];
        } else if (j + 1U == nb) {
            source = generation[nbd - 1U];
        } else {
            source = 0.5 * (generation[j - 1U] + generation[j]);
        }
        r[s.energy(j)] = e[j] - p.eddy_scale_m * p.eddy_scale_m * laplacian -
                         p.eddy_generation_m4_s2_K2 * source;
    }

    // v̄ rows and the lid.
    for (std::size_t i = 0; i < nbd; ++i) {
        S lid_sum = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t south = k * nb + i;
            const std::size_t north = south + 1U;
            const S u_b = 0.5 * (u[south] + u[north]);
            const S theta_hat = 0.5 * (t[south] + t[north]) * s.theta_hat_factor[k * nbd + i];
            const S pressure_force =
                ((geo[north] - geo[south]) +
                 s.cp * theta_hat * (s.exner_mean[north] - s.exner_mean[south])) /
                dy;
            const S div_south = divergence[south] / s.mass[i];
            const S div_north = divergence[north] / s.mass[i + 1U];
            const S vk = v[k * nbd + i];
            S dv = -(s.f_b[i] + u_b * s.tan_b[i] / a) * u_b - pressure_force - lid[i] -
                   s.friction_b[k * nbd + i] * vk +
                   p.viscosity_m2_s * (div_north - div_south) / dy;
            if (k == 0U && s.drag_b[i] > 0.0) {
                const S speed = sqrt(u_b * u_b + eddy_speed_sq(e_b[i], w2));
                dv -= s.drag_b[i] / (0.5 * (t[south] + t[north])) * speed * vk;
            }
            r[s.v(k, i)] = dv;
            lid_sum += vk;
        }
        r[s.lid(i)] = lid_sum;
    }
}

[[nodiscard]] std::size_t count_unknowns(std::size_t bands, std::size_t layers) {
    return bands * (2U * layers + 1U) + (bands - 1U) * (layers + 1U);
}

}  // namespace

ZonalCirculation::ZonalCirculation(ZonalCirculationParameters parameters)
    : parameters_(parameters) {
    const auto& p = parameters_;
    if (p.bands < 4U || p.layer_count < 2U) {
        throw std::invalid_argument("the zonal circulation needs at least 4 bands and 2 layers");
    }
    if (!(p.radius_m > 0.0) || !(p.gravity_m_s2 > 0.0) || !(p.gas_constant_J_kg_K > 0.0) ||
        !(p.heat_capacity_J_kg_K > 0.0) || !(p.reference_pressure_Pa > 0.0) ||
        !(p.rotation_rate_rad_s >= 0.0) || !(p.viscosity_m2_s >= 0.0) ||
        !(p.eddy_generation_m4_s2_K2 >= 0.0) || !(p.eddy_scale_m > 0.0) ||
        !(p.heat_mixing_length_m >= 0.0) || !(p.momentum_mixing_length_m >= 0.0) ||
        !(p.minimum_eddy_velocity_m_s > 0.0) || !(p.upwind_smoothing_m_s > 0.0) || !(p.minimum_buoyancy_frequency_sq_s2 > 0.0) ||
        !(p.tolerance > 0.0) || !(p.initial_pseudo_step_s > 0.0) ||
        !(p.pseudo_step_growth >= 1.0) || !(p.continuation_rejection >= 1.0) ||
        !(p.critical_lapse_rate_K_m >= 0.0) || !(p.convective_time_s > 0.0) ||
        !(p.vertical_viscosity_m2_s >= 0.0) ||
        !(p.convective_smoothing_K > 0.0)) {
        throw std::invalid_argument("invalid zonal circulation parameters");
    }
}

std::size_t ZonalCirculation::unknown_count() const noexcept {
    return count_unknowns(parameters_.bands, parameters_.layer_count);
}

void ZonalCirculation::check(const ZonalForcing& forcing) const {
    const std::size_t nb = parameters_.bands;
    const std::size_t layered = nb * parameters_.layer_count;
    if (forcing.surface_pressure_Pa.size() != nb || forcing.surface_height_m.size() != nb ||
        forcing.drag_coefficient.size() != nb || forcing.temperature_K.size() != layered ||
        forcing.heating_K_s.size() != layered || forcing.heating_derivative_s.size() != layered ||
        forcing.rayleigh_friction_s.size() != layered) {
        throw std::invalid_argument("zonal forcing does not match the bands and layers");
    }
    for (std::size_t j = 0; j < nb; ++j) {
        if (!(forcing.surface_pressure_Pa[j] > 0.0) || !std::isfinite(forcing.surface_height_m[j]) ||
            !(forcing.drag_coefficient[j] >= 0.0)) {
            throw std::invalid_argument("invalid zonal forcing");
        }
    }
    for (std::size_t c = 0; c < layered; ++c) {
        if (!(forcing.temperature_K[c] > 0.0) || !std::isfinite(forcing.heating_K_s[c]) ||
            !(forcing.heating_derivative_s[c] <= 0.0) || !(forcing.rayleigh_friction_s[c] >= 0.0)) {
            throw std::invalid_argument("invalid zonal forcing");
        }
    }
}

std::vector<double> ZonalCirculation::initial_state(const ZonalForcing& forcing) const {
    check(forcing);
    const auto& p = parameters_;
    const Setup s = make_setup(p, forcing);
    const std::size_t nb = s.bands;
    const std::size_t nbd = nb - 1U;
    const std::size_t n = s.layers;
    std::vector<double> x(unknown_count(), 0.0);
    std::vector<double> geo(n * nb);
    for (std::size_t j = 0; j < nb; ++j) {
        double phi = s.surface_geopotential[j];
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * nb + j;
            const double theta = forcing.temperature_K[c] / s.exner_mean[c];
            x[s.theta(k, j)] = theta;
            const double pi_bottom = s.exner_interface[c];
            const double pi_top = s.exner_interface[(k + 1U) * nb + j];
            geo[c] = phi + s.cp * theta * (pi_bottom - s.exner_mean[c]);
            phi += s.cp * theta * (pi_bottom - pi_top);
        }
    }
    // Thermal wind with zero surface wind, 1/f regularised in the tropics.
    const double f_star =
        2.0 * s.omega * std::sin(p.initial_guess_latitude_deg * pi / 180.0);
    std::vector<double> u_b(n * nbd, 0.0);
    for (std::size_t i = 0; i < nbd; ++i) {
        std::vector<double> force(n);
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t south = k * nb + i;
            const std::size_t north = south + 1U;
            const double theta_hat =
                0.5 * (forcing.temperature_K[south] + forcing.temperature_K[north]) *
                s.theta_hat_factor[k * nbd + i];
            force[k] = ((geo[north] - geo[south]) +
                        s.cp * theta_hat * (s.exner_mean[north] - s.exner_mean[south])) /
                       s.dy;
        }
        x[s.lid(i)] = -force[0];
        const double f = s.f_b[i];
        const double denominator = f * f + f_star * f_star;
        for (std::size_t k = 0; k < n; ++k) {
            u_b[k * nbd + i] = denominator > 0.0 ? -(force[k] - force[0]) * f / denominator : 0.0;
        }
    }
    for (std::size_t j = 0; j < nb; ++j) {
        for (std::size_t k = 0; k < n; ++k) {
            double sum = 0.0;
            double count = 0.0;
            if (j > 0U) {
                sum += u_b[k * nbd + j - 1U];
                count += 1.0;
            }
            if (j + 1U < nb) {
                sum += u_b[k * nbd + j];
                count += 1.0;
            }
            x[s.u(k, j)] = sum / count;
        }
    }
    return x;
}

std::vector<double> ZonalCirculation::residual(const ZonalForcing& forcing,
                                               std::span<const double> state) const {
    check(forcing);
    if (state.size() != unknown_count()) {
        throw std::invalid_argument("zonal state size does not match the unknowns");
    }
    const Setup s = make_setup(parameters_, forcing);
    std::vector<double> r(state.size());
    evaluate<double>(s, parameters_, state, r);
    return r;
}

BandedMatrix ZonalCirculation::jacobian(const ZonalForcing& forcing,
                                        std::span<const double> state) const {
    check(forcing);
    const std::size_t n = unknown_count();
    if (state.size() != n) {
        throw std::invalid_argument("zonal state size does not match the unknowns");
    }
    const Setup s = make_setup(parameters_, forcing);
    const std::size_t block = s.block;
    // Rows of block j depend on blocks j − 2 .. j + 2 only (the limited
    // face values reach two bands), so columns 5 blocks apart share no row.
    constexpr std::size_t reach = 2U;
    BandedMatrix matrix(n, (reach + 1U) * block - 1U, (reach + 1U) * block - 1U);
    const std::size_t stride = (2U * reach + 1U) * block;
    std::vector<Dual> x(n);
    std::vector<Dual> r(n);
    for (std::size_t group = 0; group < std::min(stride, n); ++group) {
        for (std::size_t c = 0; c < n; ++c) {
            x[c] = Dual(state[c], c % stride == group ? 1.0 : 0.0);
        }
        evaluate<Dual>(s, parameters_, x, r);
        for (std::size_t c = group; c < n; c += stride) {
            const std::size_t column_block = c / block;
            const std::size_t first =
                column_block > reach ? (column_block - reach) * block : 0U;
            const std::size_t last = std::min(n, (column_block + reach + 1U) * block);
            for (std::size_t row = first; row < last; ++row) {
                if (matrix.in_band(row, c)) {
                    matrix.at(row, c) = r[row].d;
                }
            }
        }
    }
    return matrix;
}

namespace {

// The residual scaled to m/s and K per day (u, θ, v rows), m²/s² (E) and
// m/s (lid).
[[nodiscard]] double scaled_entry(std::span<const double> residual, std::size_t index,
                                  std::size_t layers, std::size_t block) {
    const std::size_t within = index % block;
    const bool tendency =
        within < 2U * layers || (within > 2U * layers && within < 3U * layers + 1U);
    return residual[index] * (tendency ? seconds_per_day : 1.0);
}

}  // namespace

double ZonalCirculation::scaled_norm(std::span<const double> residual) const {
    double norm = 0.0;
    for (std::size_t index = 0; index < residual.size(); ++index) {
        const double scaled =
            std::abs(scaled_entry(residual, index, parameters_.layer_count, block_size()));
        if (!std::isfinite(scaled)) {
            return std::numeric_limits<double>::infinity();
        }
        norm = std::max(norm, scaled);
    }
    return norm;
}

double ZonalCirculation::scaled_rms(std::span<const double> residual) const {
    double sum = 0.0;
    for (std::size_t index = 0; index < residual.size(); ++index) {
        const double scaled = scaled_entry(residual, index, parameters_.layer_count, block_size());
        sum += scaled * scaled;
    }
    const double rms = std::sqrt(sum / static_cast<double>(residual.size()));
    return std::isfinite(rms) ? rms : std::numeric_limits<double>::infinity();
}

namespace {

// One Newton attempt: up to `iterations` steps with an Armijo line search
// on the scaled RMS residual. Returns whether the tolerance was reached;
// `x` is left at the last accepted iterate.
struct NewtonAttempt {
    bool converged = false;
    std::size_t steps = 0;
};

}  // namespace

ZonalCirculationSolution ZonalCirculation::solve(const ZonalForcing& forcing) const {
    check(forcing);
    const auto& p = parameters_;
    const std::size_t n = unknown_count();
    const std::size_t block = block_size();
    const std::size_t layers = p.layer_count;
    ZonalSolutionStatistics statistics;
    std::vector<double> history;

    const auto newton = [&](std::vector<double>& x, std::size_t iterations) {
        NewtonAttempt attempt;
        auto r = residual(forcing, x);
        double norm = scaled_norm(r);
        double merit = scaled_rms(r);
        for (std::size_t iteration = 0;; ++iteration) {
            history.push_back(norm);
            if (norm < p.tolerance) {
                attempt.converged = true;
                return attempt;
            }
            if (iteration == iterations || !std::isfinite(norm)) {
                return attempt;
            }
            std::vector<double> delta(r.size());
            for (std::size_t c = 0; c < n; ++c) {
                delta[c] = -r[c];
            }
            try {
                const BandedLU lu(jacobian(forcing, x));
                lu.solve(delta);
            } catch (const std::runtime_error&) {
                return attempt;
            }
            ++statistics.jacobians;
            bool accepted = false;
            double fraction = 1.0;
            for (std::size_t halving = 0; halving <= p.max_step_halvings; ++halving) {
                std::vector<double> trial = x;
                for (std::size_t c = 0; c < n; ++c) {
                    trial[c] += fraction * delta[c];
                }
                auto trial_residual = residual(forcing, trial);
                const double trial_merit = scaled_rms(trial_residual);
                if (trial_merit <= (1.0 - 1.0e-4 * fraction) * merit) {
                    x = std::move(trial);
                    r = std::move(trial_residual);
                    norm = scaled_norm(r);
                    merit = trial_merit;
                    accepted = true;
                    break;
                }
                fraction *= 0.5;
            }
            if (!accepted) {
                return attempt;
            }
            ++attempt.steps;
            ++statistics.newton_steps;
        }
    };
    const auto finish = [&](const std::vector<double>& x, ZonalSolutionMethod method) {
        auto result = solution(forcing, x);
        result.state = x;
        result.method = method;
        result.statistics = statistics;
        result.residual = history.back();
        result.residual_history = std::move(history);
        return result;
    };

    // Newton from the initial state of §14.
    const std::vector<double> start = initial_state(forcing);
    {
        std::vector<double> x = start;
        if (newton(x, p.max_newton_iterations).converged) {
            return finish(x, ZonalSolutionMethod::newton);
        }
    }

    // Pseudo-transient continuation from the same start: backward Euler in
    // ū, θ̄ and v̄, with E and the lid held. The pseudo-step grows as the RMS
    // residual falls and is cut fourfold when a step would raise it by
    // half. The steady state can be unstable to time-marching, so every
    // polish_interval steps Newton is tried from a copy of the iterate; the
    // final convergence is Newton's.
    std::vector<double> x = start;
    auto r = residual(forcing, x);
    double merit = scaled_rms(r);
    double step = p.initial_pseudo_step_s;
    std::size_t since_polish = 0;
    for (std::size_t iteration = 0;; ++iteration) {
        history.push_back(scaled_norm(r));
        if (history.back() < p.tolerance) {
            return finish(x, ZonalSolutionMethod::continuation);
        }
        if (since_polish >= p.polish_interval) {
            since_polish = 0;
            std::vector<double> polished = x;
            if (newton(polished, p.max_newton_iterations).converged) {
                return finish(polished, ZonalSolutionMethod::continuation);
            }
        }
        if (iteration == p.max_continuation_iterations || !std::isfinite(merit)) {
            throw std::runtime_error("zonal circulation: no steady state after " +
                                     std::to_string(iteration) +
                                     " continuation iterations (scaled residual " +
                                     std::to_string(history.back()) + ")");
        }
        BandedMatrix matrix = jacobian(forcing, x);
        ++statistics.jacobians;
        for (std::size_t row = 0; row < n; ++row) {
            const std::size_t first = row > matrix.lower() ? row - matrix.lower() : 0U;
            const std::size_t last = std::min(n - 1U, row + matrix.upper());
            for (std::size_t column = first; column <= last; ++column) {
                matrix.at(row, column) = -matrix.at(row, column);
            }
            const std::size_t within = row % block;
            if (within < 2U * layers || (within > 2U * layers && within < 3U * layers + 1U)) {
                matrix.at(row, row) += 1.0 / step;
            }
        }
        std::vector<double> delta = r;
        bool solved = true;
        try {
            const BandedLU lu(std::move(matrix));
            lu.solve(delta);
        } catch (const std::runtime_error&) {
            solved = false;
        }
        bool accepted = false;
        if (solved) {
            std::vector<double> trial = x;
            for (std::size_t c = 0; c < n; ++c) {
                trial[c] += delta[c];
            }
            auto trial_residual = residual(forcing, trial);
            const double trial_merit = scaled_rms(trial_residual);
            if (trial_merit <= p.continuation_rejection * merit) {
                step *= std::clamp(merit / trial_merit, 0.5, p.pseudo_step_growth);
                x = std::move(trial);
                r = std::move(trial_residual);
                merit = trial_merit;
                accepted = true;
            }
        }
        if (accepted) {
            ++statistics.continuation_steps;
            ++since_polish;
        } else {
            ++statistics.rejected_steps;
            step *= 0.25;
        }
    }
}

ZonalCirculationSolution ZonalCirculation::solution(const ZonalForcing& forcing,
                                                    std::span<const double> x) const {
    const auto& p = parameters_;
    const Setup s = make_setup(p, forcing);
    const std::size_t nb = s.bands;
    const std::size_t nbd = nb - 1U;
    const std::size_t n = s.layers;
    const double a = s.a;
    const double n_d = static_cast<double>(n);
    ZonalCirculationSolution out;
    out.bands = nb;
    out.layers = n;
    out.latitude_deg.resize(nb);
    out.boundary_latitude_deg.resize(nbd);
    out.eastward_m_s.resize(n * nb);
    out.temperature_K.resize(n * nb);
    out.northward_m_s.resize(n * nbd);
    out.heat_diffusivity_m2_s.assign(n * nbd, 0.0);
    out.streamfunction_kg_s.assign((n + 1U) * nbd, 0.0);
    out.eddy_kinetic_m2_s2.resize(nb);
    out.eddy_momentum_flux_m2_s2.assign(nbd, 0.0);
    out.barotropic_gradient_m_s2.resize(nbd);
    out.surface_pressure_Pa.resize(nb);
    out.surface_torque_N_m.assign(nb, 0.0);
    const double w2 = p.minimum_eddy_velocity_m_s * p.minimum_eddy_velocity_m_s;

    for (std::size_t j = 0; j < nb; ++j) {
        out.latitude_deg[j] = s.latitude[j] * 180.0 / pi;
        out.eddy_kinetic_m2_s2[j] = x[s.energy(j)];
        for (std::size_t k = 0; k < n; ++k) {
            out.eastward_m_s[k * nb + j] = x[s.u(k, j)];
            out.temperature_K[k * nb + j] = x[s.theta(k, j)] * s.exner_mean[k * nb + j];
        }
    }
    for (std::size_t i = 0; i < nbd; ++i) {
        out.boundary_latitude_deg[i] = s.latitude_b[i] * 180.0 / pi;
        out.barotropic_gradient_m_s2[i] = x[s.lid(i)];
        double psi = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            out.northward_m_s[k * nbd + i] = x[s.v(k, i)];
            psi += 2.0 * pi * a * s.cos_b[i] * s.mass_b[i] * x[s.v(k, i)];
            out.streamfunction_kg_s[(k + 1U) * nbd + i] = psi;
        }
        if (p.eddies) {
            const double e_b = 0.5 * (x[s.energy(i)] + x[s.energy(i + 1U)]);
            const double speed = std::sqrt(eddy_speed_sq(e_b, w2));
            for (std::size_t k = 0; k < n; ++k) {
                out.heat_diffusivity_m2_s[k * nbd + i] =
                    p.heat_mixing_length_m * speed * s.heat_shape[k];
            }
            out.eddy_momentum_flux_m2_s2[i] = p.momentum_mixing_length_m *
                                              (x[s.energy(i + 1U)] - x[s.energy(i)]) / s.dy;
        }
    }

    // Balanced zonal-mean p_s: ∂ ln p_s/∂y = P / (R T̄_col), total mass held.
    std::vector<double> log_change(nb, 0.0);
    for (std::size_t i = 0; i < nbd; ++i) {
        double t_column = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            t_column += 0.5 * (out.temperature_K[k * nb + i] + out.temperature_K[k * nb + i + 1U]) /
                        n_d;
        }
        log_change[i + 1U] =
            log_change[i] + out.barotropic_gradient_m_s2[i] * s.dy / (s.gas * t_column);
    }
    double mass = 0.0;
    double mass_changed = 0.0;
    for (std::size_t j = 0; j < nb; ++j) {
        mass += s.area[j] * forcing.surface_pressure_Pa[j];
        mass_changed += s.area[j] * forcing.surface_pressure_Pa[j] * std::exp(log_change[j]);
    }
    for (std::size_t j = 0; j < nb; ++j) {
        out.surface_pressure_Pa[j] =
            forcing.surface_pressure_Pa[j] * std::exp(log_change[j]) * mass / mass_changed;
    }

    // Surface torque: Rayleigh friction and bottom drag on each band's
    // column, ∫ μ a cos φ (−k_v ū − drag) dA.
    for (std::size_t j = 0; j < nb; ++j) {
        const double band_area = 2.0 * pi * a * a * s.area[j];
        double torque = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * nb + j;
            const double u = out.eastward_m_s[c];
            double acceleration = -s.friction[c] * u;
            if (k == 0U && s.drag_band[j] > 0.0) {
                const double speed =
                    std::sqrt(u * u + eddy_speed_sq(x[s.energy(j)], w2));
                acceleration -= s.drag_band[j] / out.temperature_K[c] * speed * u;
            }
            torque += s.mass[j] * a * s.cos_band[j] * acceleration;
        }
        out.surface_torque_N_m[j] = torque * band_area;
        out.total_torque_N_m += out.surface_torque_N_m[j];
        out.gross_torque_N_m += std::abs(out.surface_torque_N_m[j]);
    }
    return out;
}

ZonalForcing held_suarez_zonal_forcing(const ZonalCirculationParameters& parameters,
                                       const HeldSuarezForcing& hs) {
    const std::size_t nb = parameters.bands;
    const std::size_t n = parameters.layer_count;
    const double n_d = static_cast<double>(n);
    const double p0 = parameters.reference_pressure_Pa;
    const double kappa = parameters.gas_constant_J_kg_K / parameters.heat_capacity_J_kg_K;
    ZonalForcing forcing;
    forcing.surface_pressure_Pa.assign(nb, p0);
    forcing.surface_height_m.assign(nb, 0.0);
    forcing.drag_coefficient.assign(nb, 0.0);
    forcing.temperature_K.resize(n * nb);
    forcing.heating_K_s.assign(n * nb, 0.0);
    forcing.heating_derivative_s.resize(n * nb);
    forcing.rayleigh_friction_s.resize(n * nb);
    std::vector<double> interface_exner;
    std::vector<double> mean_exner;
    layer_exner(p0, n, p0, kappa, interface_exner, mean_exner);
    const double dphi = pi / static_cast<double>(nb);
    for (std::size_t j = 0; j < nb; ++j) {
        const double latitude = -0.5 * pi + dphi * (static_cast<double>(j) + 0.5);
        const double s2 = std::sin(latitude) * std::sin(latitude);
        const double c2 = 1.0 - s2;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t c = k * nb + j;
            const double pi_mean = mean_exner[k];
            const double pressure = p0 * std::pow(pi_mean, 1.0 / kappa);
            const double sigma = pressure / p0;
            const double t_eq = std::max(
                200.0, (315.0 - hs.equator_pole_K * s2 -
                        hs.static_stability_K * std::log(pressure / p0) * c2) *
                           pi_mean);
            const double factor = std::max(
                0.0, (sigma - hs.boundary_layer_sigma) / (1.0 - hs.boundary_layer_sigma));
            const double k_t = 1.0 / hs.relaxation_free_s +
                               (1.0 / hs.relaxation_surface_s - 1.0 / hs.relaxation_free_s) *
                                   factor * c2 * c2;
            forcing.temperature_K[c] = t_eq;
            forcing.heating_derivative_s[c] = -k_t;
            // The core's friction uses the layer's mid-σ.
            const double sigma_mid = 1.0 - (static_cast<double>(k) + 0.5) / n_d;
            const double friction_factor = std::max(
                0.0, (sigma_mid - hs.boundary_layer_sigma) / (1.0 - hs.boundary_layer_sigma));
            forcing.rayleigh_friction_s[c] = friction_factor / hs.friction_s;
        }
    }
    return forcing;
}

}  // namespace planetsim

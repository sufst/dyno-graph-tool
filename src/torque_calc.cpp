#include "torque_calc.h"
#include <algorithm>
#include <map>

constexpr double nm_to_ftlb  = 0.7375621;
constexpr double rpm_nm_to_kw = 9549.2968;

torque_curve compute_torque(const dpr_run& run, int buf_size) {
    torque_curve out;

    if (!run.has_channel(ch_elapsed_time) || !run.has_channel(ch_roller_omega) ||
        !run.has_channel(ch_engine_rpm) || !run.has_channel(ch_wheel_speed))
        return out;

    auto& t_raw     = run.channel(ch_elapsed_time);
    auto& omega_raw = run.channel(ch_roller_omega);
    auto& rpm_raw   = run.channel(ch_engine_rpm);
    auto& speed_raw = run.channel(ch_wheel_speed);
    int n = run.num_rows;

    struct bucket { double omega = 0, rpm = 0, speed = 0; int count = 0; };
    std::map<double, bucket> buckets;
    for (int i = 0; i < n; ++i) {
        auto& b = buckets[t_raw[i]];
        b.omega += omega_raw[i];
        b.rpm   += rpm_raw[i];
        b.speed += speed_raw[i];
        b.count++;
    }

    int nu = static_cast<int>(buckets.size());
    std::vector<double> ut(nu), uo(nu), ur(nu), us(nu);
    int j = 0;
    for (auto& [t, b] : buckets) {
        ut[j] = t;
        uo[j] = b.omega / b.count;
        ur[j] = b.rpm   / b.count;
        us[j] = b.speed / b.count;
        ++j;
    }

    int half = buf_size / 2;
    std::vector<double> alpha(nu, 0.0);
    for (int i = 0; i < nu; ++i) {
        int lo = std::max(0, i - half), hi = std::min(nu - 1, i + half);
        double dt = ut[hi] - ut[lo];
        if (dt > 0) alpha[i] = (uo[hi] - uo[lo]) / dt;
    }

    auto& fp = run.header.friction_poly;
    std::vector<double> friction(nu);
    for (int i = 0; i < nu; ++i) {
        double v = us[i];
        friction[i] = fp[0]*v*v*v + fp[1]*v*v + fp[2]*v + fp[3];
    }

    double I = run.header.roller_inertia;
    out.time.resize(nu);
    out.rpm.resize(nu);
    out.speed_mph.resize(nu);
    out.torque_nm.resize(nu);
    out.power_kw.resize(nu);

    for (int i = 0; i < nu; ++i) {
        out.time[i]      = ut[i];
        out.rpm[i]       = ur[i];
        out.speed_mph[i] = us[i];
        out.torque_nm[i] = (friction[i] / nm_to_ftlb) + I * alpha[i];
        out.power_kw[i]  = out.torque_nm[i] * ur[i] / rpm_nm_to_kw;
    }

    out.peak_rpm_idx = static_cast<int>(std::distance(
        out.rpm.begin(), std::max_element(out.rpm.begin(), out.rpm.end())));

    return out;
}

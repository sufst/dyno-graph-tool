#pragma once
#include "dpr_parser.h"
#include <vector>

struct torque_curve {
    std::vector<double> time;
    std::vector<double> rpm;
    std::vector<double> speed_mph;
    std::vector<double> torque_nm;
    std::vector<double> power_kw;
    int peak_rpm_idx = -1;
};

torque_curve compute_torque(const dpr_run& run, int buf_size = 51);

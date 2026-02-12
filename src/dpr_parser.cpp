#include "dpr_parser.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>

static auto read_csv_records(std::istream& is) {
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> row;
    std::string field;
    bool in_q = false;

    auto content = std::string(std::istreambuf_iterator<char>(is), {});

    for (size_t i = 0; i < content.size(); ++i) {
        auto c = content[i];
        if (in_q) {
            if (c == '"') {
                if (i + 1 < content.size() && content[i + 1] == '"') { field += '"'; ++i; }
                else in_q = false;
            } else field += c;
        } else {
            if (c == '"') in_q = true;
            else if (c == ',') { row.push_back(field); field.clear(); }
            else if (c == '\r') {}
            else if (c == '\n') { row.push_back(field); field.clear(); records.push_back(std::move(row)); row.clear(); }
            else field += c;
        }
    }
    if (!field.empty() || !row.empty()) { row.push_back(field); records.push_back(std::move(row)); }
    return records;
}

static double to_double(const std::string& s, double def = 0.0) {
    if (s.empty()) return def;
    char* end = nullptr;
    auto v = std::strtod(s.c_str(), &end);
    return (end != s.c_str()) ? v : def;
}

static int to_int(const std::string& s, int def = 0) { return static_cast<int>(to_double(s, def)); }

static bool is_numeric(const std::string& s) {
    if (s.empty() || s == "#TRUE#" || s == "#FALSE#" || s == "-" || s == "+") return true;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

static bool is_data_row(const std::vector<std::string>& row, int min_cols, int min_num, double ratio) {
    if (std::ssize(row) < min_cols) return false;
    int ne = 0, nu = 0;
    for (auto& f : row) { if (!f.empty()) { ++ne; if (is_numeric(f)) ++nu; } }
    return ne > 0 && nu >= min_num && static_cast<double>(nu) / ne >= ratio;
}

static std::string hex_decode(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(hex[i]), l = nib(hex[i + 1]);
        if (h < 0 || l < 0) return hex;
        out += static_cast<char>(h * 16 + l);
    }
    return out;
}

static auto& f(const std::vector<std::string>& row, int i) {
    static const std::string empty;
    return (i >= 0 && i < std::ssize(row)) ? row[i] : empty;
}

static dpr_header parse_header(const std::vector<std::vector<std::string>>& rows) {
    dpr_header h;
    if (rows.size() > 1) { auto& r = rows[1]; h.date = f(r,0); h.time = f(r,1); h.filename = f(r,2); h.run_number = to_int(f(r,3)); if (r.size() > 5) h.run_name = f(r,5); }
    if (rows.size() > 2) { auto& r = rows[2]; h.ambient_temp_c = to_double(f(r,0)); h.ambient_press_mb = to_double(f(r,1)); h.ambient_humid_pct = to_double(f(r,2)); h.correction_factor = to_double(f(r,3)); }
    if (rows.size() > 4) { auto& r = rows[4]; h.roller_circ_ft = to_double(hex_decode(f(r,0))); h.roller_diam_in = to_double(hex_decode(f(r,1))); h.wheel_circ_m = to_double(hex_decode(f(r,4))); h.gear_ratio = to_double(f(r,6)); h.machine_sub = f(r,7); for (int i = 0; i < 4; ++i) h.friction_poly[i] = to_double(f(r, 10 + i)); }
    if (rows.size() > 5) { auto& r = rows[5]; h.manufacturer = f(r,0); h.model = f(r,1); h.machine_type = f(r,2); h.opto_slots = to_int(f(r,3)); h.software_version = f(r,6); }
    if (rows.size() > 6) { auto& r = rows[6]; h.peak_power_hp = to_double(f(r,0)); h.peak_power_rpm = to_double(f(r,1)); h.peak_torque_ftlb = to_double(f(r,2)); h.peak_torque_rpm = to_double(f(r,3)); }
    if (rows.size() > 7) { auto& r = rows[7]; h.roller_inertia = to_double(f(r,7)); }
    return h;
}

std::optional<dpr_run> parse_dpr_file(const std::string& path, std::string& err) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { err = "cannot open: " + path; return std::nullopt; }

    auto records = read_csv_records(ifs);
    if (records.size() < 42) { err = "file too short (" + std::to_string(records.size()) + " rows)"; return std::nullopt; }

    int best_start = -1, best_end = -1, best_len = 0, seg_start = -1;
    for (int i = 0; i < std::ssize(records); ++i) {
        if (is_data_row(records[i], 20, 10, 0.9)) {
            if (seg_start < 0) seg_start = i;
        } else if (seg_start >= 0) {
            if (int len = i - seg_start; len > best_len) { best_start = seg_start; best_end = i - 1; best_len = len; }
            seg_start = -1;
        }
    }
    if (seg_start >= 0) { if (int len = std::ssize(records) - seg_start; len > best_len) { best_start = seg_start; best_end = std::ssize(records) - 1; best_len = len; } }
    if (best_len < 50) { err = "no data block found (" + std::to_string(best_len) + " rows)"; return std::nullopt; }

    int width = 0;
    for (int i = best_start; i <= best_end; ++i) width = std::max(width, static_cast<int>(records[i].size()));

    dpr_run run;
    run.header = parse_header({records.begin(), records.begin() + best_start});
    run.num_rows = best_end - best_start + 1;
    run.num_columns = std::min(width, num_channels);

    run.data.resize(run.num_columns);
    for (auto& ch : run.data) ch.resize(run.num_rows);

    for (int r = 0; r < run.num_rows; ++r) {
        auto& row = records[best_start + r];
        for (int c = 0; c < run.num_columns; ++c)
            run.data[c][r] = (c < std::ssize(row)) ? to_double(row[c]) : 0.0;
    }
    return run;
}

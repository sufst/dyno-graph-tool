#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "dpr_parser.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "portable-file-dialogs.h"
#include "torque_calc.h"

enum class series : int {
  none = -1,
  rpm = 0,
  time,
  speed,
  torque,
  power,
  count
};

constexpr const char* series_label(series s) {
  switch (s) {
    case series::rpm:
      return "RPM";
    case series::time:
      return "Time (s)";
    case series::speed:
      return "Speed (mph)";
    case series::torque:
      return "Torque (Nm)";
    case series::power:
      return "Power (kW)";
    default:
      return "(none)";
  }
}

static const double* series_data(const torque_curve& c, series s) {
  switch (s) {
    case series::rpm:
      return c.rpm.data();
    case series::time:
      return c.time.data();
    case series::speed:
      return c.speed_mph.data();
    case series::torque:
      return c.torque_nm.data();
    case series::power:
      return c.power_kw.data();
    default:
      return nullptr;
  }
}

struct graph {
  series x = series::none;
  series y = series::none;
  int id = 0;
  bool fit = true;
};

struct app_state {
  std::optional<dpr_run> run;
  std::optional<torque_curve> curve;
  std::string filepath;
  std::string load_error;
  std::vector<graph> graphs;
  int next_id = 1;
};

static void open_file(app_state& app, const std::string& path) {
  app.load_error.clear();
  app.run.reset();
  app.curve.reset();
  app.filepath = path;
  auto result = parse_dpr_file(path, app.load_error);
  if (!result) return;
  app.run = std::move(*result);
  app.curve = compute_torque(*app.run);
}

static bool axis_menu(const char* id, series& current) {
  bool changed = false;
  if (ImGui::BeginPopup(id)) {
    for (int i = 0; i < static_cast<int>(series::count); ++i) {
      auto s = static_cast<series>(i);
      if (ImGui::MenuItem(series_label(s), nullptr, current == s)) {
        current = s;
        changed = true;
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("(none)", nullptr, current == series::none)) {
      current = series::none;
      changed = true;
    }
    ImGui::EndPopup();
  }
  return changed;
}

static void draw_ui(app_state& app) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open .Dpr...", "Ctrl+O")) {
        auto f = pfd::open_file("Open .Dpr", ".",
                                {"DynaRun files", "*.Dpr *.dpr", "All", "*"});
        if (auto sel = f.result(); !sel.empty()) open_file(app, sel[0]);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit", "Ctrl+Q")) std::exit(0);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  auto* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("##main", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  if (!app.run) {
    auto msg = app.load_error.empty()
                   ? "drag & drop a .Dpr file or use File > Open"
                   : app.load_error.c_str();
    auto ts = ImGui::CalcTextSize(msg);
    ImGui::SetCursorPos({vp->WorkSize.x * 0.5f - ts.x * 0.5f,
                         vp->WorkSize.y * 0.5f - ts.y * 0.5f});
    ImGui::TextUnformatted(msg);
    ImGui::End();
    return;
  }

  auto& hdr = app.run->header;

  ImGui::BeginChild("##info", {260, 0}, ImGuiChildFlags_Border);

  ImGui::SeparatorText("run");
  ImGui::Text("%s", app.filepath.c_str());
  ImGui::Text("%s  %s", hdr.date.c_str(), hdr.time.c_str());
  ImGui::Text("%s %s", hdr.manufacturer.c_str(), hdr.model.c_str());
  ImGui::Text("%d samples", app.run->num_rows);

  ImGui::SeparatorText("ambient");
  ImGui::Text("%.1f C  %.0f mbar  %.0f%%", hdr.ambient_temp_c,
              hdr.ambient_press_mb, hdr.ambient_humid_pct);
  ImGui::Text("CF %.3f", hdr.correction_factor);

  ImGui::SeparatorText("machine");
  ImGui::Text("inertia  %.4f kg.m2", hdr.roller_inertia);
  ImGui::Text("gear     %.4f", hdr.gear_ratio);
  ImGui::Text("wheel    %.4f m", hdr.wheel_circ_m);

  ImGui::SeparatorText("peaks");
  ImGui::Text("%.1f kW @ %.0f rpm", hdr.peak_power_hp * 0.7457,
              hdr.peak_power_rpm);
  ImGui::Text("%.1f Nm @ %.0f rpm", hdr.peak_torque_ftlb / 0.7375621,
              hdr.peak_torque_rpm);

  ImGui::SeparatorText("graphs");
  if (ImGui::Button("+ add graph", {-1, 0}))
    app.graphs.push_back({.id = app.next_id++});

  int del = -1;
  for (int i = 0; i < std::ssize(app.graphs); ++i) {
    auto& g = app.graphs[i];
    ImGui::PushID(g.id);
    auto* yn = g.y != series::none ? series_label(g.y) : "?";
    auto* xn = g.x != series::none ? series_label(g.x) : "?";
    ImGui::BulletText("%s vs %s", yn, xn);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) del = i;
    ImGui::PopID();
  }
  if (del >= 0) app.graphs.erase(app.graphs.begin() + del);

  ImGui::EndChild();
  ImGui::SameLine();

  ImGui::BeginChild("##plots");

  if (app.graphs.empty()) {
    auto avail = ImGui::GetContentRegionAvail();
    auto ts = ImGui::CalcTextSize("+ add graph");
    ImGui::SetCursorPos(
        {avail.x * 0.5f - ts.x * 0.5f, avail.y * 0.5f - ts.y * 0.5f});
    ImGui::TextDisabled("+ add graph");
  } else if (app.curve && !app.curve->rpm.empty()) {
    auto& c = *app.curve;
    int n = static_cast<int>(c.rpm.size());
    auto region = ImGui::GetContentRegionAvail();
    int num = static_cast<int>(app.graphs.size());
    float h = (region.y - (num - 1) * ImGui::GetStyle().ItemSpacing.y) / num;
    if (h < 120) h = 120;

    for (auto& g : app.graphs) {
      ImGui::PushID(g.id);

      char title[128];
      if (g.x != series::none && g.y != series::none)
        snprintf(title, sizeof(title), "%s vs %s###p%d", series_label(g.y),
                 series_label(g.x), g.id);
      else
        snprintf(title, sizeof(title), "right-click an axis###p%d", g.id);

      if (ImPlot::BeginPlot(title, {region.x, h}, ImPlotFlags_NoBoxSelect)) {
        auto* xl = g.x != series::none ? series_label(g.x) : "X (right-click)";
        auto* yl = g.y != series::none ? series_label(g.y) : "Y (right-click)";
        ImPlot::SetupAxes(xl, yl);

        bool has_data = false;
        if (g.x != series::none && g.y != series::none) {
          auto* xd = series_data(c, g.x);
          auto* yd = series_data(c, g.y);
          if (xd && yd) {
            has_data = true;
            if (g.fit) {
              double xmin = *std::min_element(xd, xd + n);
              double xmax = *std::max_element(xd, xd + n);
              double ymin = *std::min_element(yd, yd + n);
              double ymax = *std::max_element(yd, yd + n);
              double xp = std::max((xmax - xmin) * 0.05, 1.0);
              double yp = std::max((ymax - ymin) * 0.05, 1.0);
              ImPlot::SetupAxisLimits(ImAxis_X1, xmin - xp, xmax + xp,
                                      ImPlotCond_Always);
              ImPlot::SetupAxisLimits(ImAxis_Y1, ymin - yp, ymax + yp,
                                      ImPlotCond_Always);
              g.fit = false;
            }
            ImPlot::PlotLine("##d", xd, yd, n);
          }
        }

        char xpop[32], ypop[32];
        snprintf(xpop, sizeof(xpop), "xm%d", g.id);
        snprintf(ypop, sizeof(ypop), "ym%d", g.id);

        if (ImPlot::IsAxisHovered(ImAxis_X1) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          ImGui::OpenPopup(xpop);
        if (ImPlot::IsAxisHovered(ImAxis_Y1) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
          ImGui::OpenPopup(ypop);

        if (axis_menu(xpop, g.x)) g.fit = true;
        if (axis_menu(ypop, g.y)) g.fit = true;

        if (has_data && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            ImPlot::IsPlotHovered())
          g.fit = true;

        ImPlot::EndPlot();
      }
      ImGui::PopID();
    }
  }

  ImGui::EndChild();
  ImGui::End();
}

static app_state* g_app = nullptr;

static void drop_cb(GLFWwindow*, int count, const char** paths) {
  if (count > 0 && g_app) open_file(*g_app, paths[0]);
}

int main(int argc, char** argv) {
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  auto* window = glfwCreateWindow(1280, 800, "dyno viewer", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  app_state app;
  g_app = &app;
  glfwSetDropCallback(window, drop_cb);
  if (argc > 1) open_file(app, argv[1]);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_ui(app);
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
}

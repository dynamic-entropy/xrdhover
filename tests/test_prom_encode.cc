#include "xrdhover/metrics.hh"
#include "xrdhover/prom_encode.hh"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using xrdhover::EncodePrometheusText;
using xrdhover::ErrorClass;
using xrdhover::MetricsRegistry;

TEST(PromEncode, ContainsCoreSeriesAndHistogramBuckets) {
    MetricsRegistry reg;
    reg.SetLabels("run-a", "host1", "default", "root://localhost/");
    reg.SetConfigGauges(10 * 1024 * 1024, 4);
    reg.ObserveSessionOk(1024, 2, 0.01, 0.02, 0.5, 1.0, "srv-a:1094");
    reg.ObserveSessionFail(ErrorClass::Timeout, "srv-a:1094");
    reg.SetInflight(1, 2);
    reg.SampleProc();

    auto snap1 = reg.Snapshot(1.25);
    EXPECT_NEAR(snap1.achieved_rate_bytes, 1024.0 / 1.25, 1e-6);

    reg.ObserveSessionOk(1024, 1, 0.01, 0.02, 0.5, 1.0, "srv-b:1094");
    auto snap2 = reg.Snapshot(2.25);
    EXPECT_NEAR(snap2.wall_s, 2.25, 1e-9);
    EXPECT_NEAR(snap2.achieved_rate_bytes, 2048.0 / 2.25, 1e-6);

    const std::string text = EncodePrometheusText(snap2);
    EXPECT_NE(text.find("xrdhover_bytes_read_total{"), std::string::npos);
    EXPECT_NE(text.find("src_dst=\"run-a\""), std::string::npos);
    EXPECT_NE(text.find("job_id=\"host1\""), std::string::npos);
    EXPECT_NE(text.find("xrdhover_sessions_total{"), std::string::npos);
    EXPECT_NE(text.find("result=\"ok\""), std::string::npos);
    EXPECT_NE(text.find("result=\"fail\""), std::string::npos);
    EXPECT_NE(text.find("# TYPE xrdhover_open_seconds histogram"), std::string::npos);
    EXPECT_NE(text.find("xrdhover_open_seconds_bucket{"), std::string::npos);
    EXPECT_NE(text.find("le=\"+Inf\""), std::string::npos);
    EXPECT_NE(text.find("xrdhover_open_seconds_sum{"), std::string::npos);
    EXPECT_NE(text.find("xrdhover_open_seconds_count{"), std::string::npos);
    EXPECT_NE(text.find("class=\"timeout\""), std::string::npos);
    EXPECT_NE(text.find("xrdhover_target_rate_bytes{"), std::string::npos);
    EXPECT_NE(text.find("xrdhover_achieved_rate_bytes{"), std::string::npos);
    EXPECT_NE(text.find("xrdhover_endpoint_bytes_total{"), std::string::npos);
    EXPECT_NE(text.find("xrdhover_endpoint_achieved_rate_bytes{"), std::string::npos);
    EXPECT_NE(text.find("data_server=\"srv-a:1094\""), std::string::npos);
    EXPECT_NE(text.find("data_server=\"srv-b:1094\""), std::string::npos);
    EXPECT_NE(text.find("xrdhover_endpoint_sessions_total{"), std::string::npos);
}

TEST(PromEncode, SourceDestFromRunId) {
    MetricsRegistry reg;
    reg.SetLabels("T2_CH_CERN__T1_DE_KIT", "T2_CH_CERN__T1_DE_KIT", "T2_CH_CERN__T1_DE_KIT",
                  "root://eoscms.cern.ch:1094/");
    const std::string text = EncodePrometheusText(reg.Snapshot(1.0));
    EXPECT_NE(text.find("source=\"T2_CH_CERN\""), std::string::npos);
    EXPECT_NE(text.find("dest=\"T1_DE_KIT\""), std::string::npos);
}

TEST(PromEncode, AllMetricNamesUseXrdhoverPrefix) {
    MetricsRegistry reg;
    reg.SetLabels("run-a", "host1", "default", "root://localhost/");
    reg.SetConfigGauges(10 * 1024 * 1024, 4);
    reg.ObserveSessionOk(1024, 2, 0.01, 0.02, 0.5, 1.0, "srv-a:1094", "T2_UK_SGrid");
    reg.ObserveSessionFail(ErrorClass::Timeout, "srv-a:1094", "T2_UK_SGrid");
    reg.ObserveSoftFault("connection");
    reg.SetInflight(1, 2);
    reg.SampleProc();

    const std::string text = EncodePrometheusText(reg.Snapshot(1.25));
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> names;
    while (std::getline(in, line)) {
        constexpr char kHelp[] = "# HELP ";
        if (line.compare(0, sizeof(kHelp) - 1, kHelp) != 0) continue;
        const std::string rest = line.substr(sizeof(kHelp) - 1);
        const auto space = rest.find(' ');
        ASSERT_NE(space, std::string::npos) << line;
        names.push_back(rest.substr(0, space));
    }
    ASSERT_FALSE(names.empty());
    for (const auto& name : names) {
        EXPECT_EQ(name.compare(0, 9, "xrdhover_"), 0) << name;
        EXPECT_NE(name.compare(0, 8, "process_"), 0) << name;
        EXPECT_NE(name.compare(0, 3, "go_"), 0) << name;
    }
}

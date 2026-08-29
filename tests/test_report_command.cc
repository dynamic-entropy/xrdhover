#include "xrdhover/report_command.hh"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using json = nlohmann::json;
using xrdhover::DiscoverResultFiles;
using xrdhover::ReportOptions;
using xrdhover::RunReportCommand;
namespace fs = std::filesystem;

namespace {

json MakeResult(const std::string& run_id, const std::string& job_id, uint64_t bytes,
                uint64_t ok, uint64_t fail, double elapsed, const std::string& site,
                const std::string& server) {
    json by_ds = json::object();
    by_ds[server] = {{"bytes_read", bytes},
                     {"sessions_ok", ok},
                     {"sessions_fail", fail},
                     {"errors", json::object()},
                     {"cms_site", site}};
    json by_site = json::object();
    by_site[site] = {{"bytes_read", bytes},
                     {"sessions_ok", ok},
                     {"sessions_fail", fail},
                     {"errors", json::object()}};
    return {{"run_id", run_id},
            {"job_id", job_id},
            {"target", "default"},
            {"endpoint", "root://example/"},
            {"elapsed_s", elapsed},
            {"bytes_read", bytes},
            {"sessions_ok", ok},
            {"sessions_fail", fail},
            {"ops", ok},
            {"errors", json::object()},
            {"soft_faults", json::object()},
            {"by_data_server", by_ds},
            {"by_cms_site", by_site}};
}

void WriteResult(const fs::path& path, const json& j) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << j.dump(2) << '\n';
}

}  // namespace

class ReportCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("xrdhover_report_" + std::to_string(::getpid()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
};

TEST_F(ReportCommandTest, DiscoversSingleRunDir) {
    const auto result = dir_ / "global" / "result.json";
    WriteResult(result, MakeResult("global", "host-a", 1000, 2, 0, 10.0, "SITE_A", "ds1:1094"));

    ReportOptions opts;
    opts.path = (dir_ / "global").string();
    const auto files = DiscoverResultFiles(opts);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], result.string());
}

TEST_F(ReportCommandTest, DiscoversResultJsonFile) {
    const auto result = dir_ / "global" / "result.json";
    WriteResult(result, MakeResult("global", "host-a", 1000, 2, 0, 10.0, "SITE_A", "ds1:1094"));

    ReportOptions opts;
    opts.path = result.string();
    const auto files = DiscoverResultFiles(opts);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], result.string());
}

TEST_F(ReportCommandTest, DiscoversViaResultsDirAndRunId) {
    const auto result = dir_ / "global" / "result.json";
    WriteResult(result, MakeResult("global", "host-a", 1000, 2, 0, 10.0, "SITE_A", "ds1:1094"));

    ReportOptions opts;
    opts.results_dir = dir_.string();
    opts.run_id = "global";
    const auto files = DiscoverResultFiles(opts);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0], result.string());
}

TEST_F(ReportCommandTest, RunReportJsonExit0) {
    WriteResult(dir_ / "global" / "result.json",
                MakeResult("global", "host-a", 5000, 5, 0, 10.0, "SITE_A", "ds1:1094"));

    ReportOptions opts;
    opts.path = (dir_ / "global").string();
    opts.json = true;
    EXPECT_EQ(RunReportCommand(opts), 0);
}

TEST_F(ReportCommandTest, MissingResultsExit2) {
    ReportOptions opts;
    opts.results_dir = dir_.string();
    opts.run_id = "missing";
    EXPECT_EQ(RunReportCommand(opts), 2);
}

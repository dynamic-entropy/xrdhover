#include "xrdhover/push_group.hh"

#include <gtest/gtest.h>

using xrdhover::FormatPushGroupPath;
using xrdhover::PushReplicaValue;
using xrdhover::kPushReplicaLabel;

TEST(PushGroup, ReplicaLabelIsReplica) { EXPECT_STREQ(kPushReplicaLabel, "replica"); }

TEST(PushGroup, ReplicaFallsBackToLocal) {
    EXPECT_EQ(PushReplicaValue(""), "local");
    EXPECT_EQ(PushReplicaValue("T2_CH_CERN__T1_DE_KIT__0"), "T2_CH_CERN__T1_DE_KIT__0");
}

TEST(PushGroup, PathIncludesReplica) {
    EXPECT_EQ(FormatPushGroupPath("xrdhover", "T2_CH_CERN__T1_DE_KIT", "T2_CH_CERN__T1_DE_KIT__0"),
              "/metrics/job/xrdhover/src_dst/T2_CH_CERN__T1_DE_KIT/replica/T2_CH_CERN__T1_DE_KIT__0");
}

TEST(PushGroup, TwoJobsOnSameLinkDifferOnlyByReplica) {
    const auto a = FormatPushGroupPath("xrdhover", "S__D", "S__D__0");
    const auto b = FormatPushGroupPath("xrdhover", "S__D", "S__D__1");
    EXPECT_NE(a, b);
    EXPECT_NE(a.find("/src_dst/S__D/"), std::string::npos);
    EXPECT_NE(a.find("/replica/S__D__0"), std::string::npos);
    EXPECT_NE(b.find("/replica/S__D__1"), std::string::npos);
}
